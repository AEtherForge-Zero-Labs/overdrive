/* Input decoder: R104's two layers over untrusted bytes.
 *
 * Layer 1 is ECMA-48 syntax: C0 controls, ESC + byte, SS3 + byte, and
 * CSI parameter bytes (0x30-0x3F), intermediate bytes (0x20-0x2F), and a
 * final byte (0x40-0x7E) (ASSUMED ranges, R104). A byte outside the
 * grammar ends the sequence and is decoded afresh. Layer 2 is the
 * dispatch table (design D4); anything it does not name is discarded.
 * Paste mode bypasses layer 2 (R103, design D5).
 *
 * Owning module (Art. IV): the partial sequence outlives a call. Reads
 * no clock: time is the caller's argument (R109, design D3). Allocates
 * nothing. */
#include "decode.h"

#include "unicode.h"

#include <string.h>

constexpr unsigned char ESC = 0x1b;

/* ESC [ 2 0 1 ~ ends a paste (xterm, Bracketed Paste Mode). */
static const char paste_end[] = "\x1b[201~";
constexpr uint8_t PASTE_END_LENGTH = 6;
static_assert(sizeof paste_end - 1 == PASTE_END_LENGTH);
static_assert(OVD_DECODER_MARKER_HELD_MAX == PASTE_END_LENGTH - 1);

static const char replacement[] = "\xEF\xBF\xBD"; /* U+FFFD */
constexpr uint32_t REPLACEMENT_CODE_POINT = 0xFFFD;

/* The largest wire value read: a 1-based coordinate whose 0-based value
 * is INT32_MAX (design D4). Every other accepted value is smaller. */
constexpr uint64_t VALUE_MAX = (uint64_t)INT32_MAX + 1;
constexpr size_t VALUES_MAX = 3;

/* How a byte was handled. */
typedef enum Step : uint8_t {
    STEP_CONSUMED,       /* taken, no event */
    STEP_EVENT,          /* taken, *event is complete */
    STEP_EVENT_KEPT,     /* not taken, *event is complete */
    STEP_AGAIN,          /* not taken, no event: decode it afresh */
} Step;

/* ---- Events ----------------------------------------------------------- */

static void clear(OvdEvent *event)
{
    memset(event, 0, sizeof *event);
}

static void key(OvdEvent *event, OvdKey which, uint32_t code_point, uint8_t modifiers)
{
    clear(event);
    event->kind = OVD_EVENT_KIND_KEY;
    event->key.key = which;
    event->key.codepoint = which == OVD_KEY_CHAR ? code_point : 0;
    event->key.modifiers = modifiers;
}

static void simple(OvdEvent *event, OvdEventKind kind)
{
    clear(event);
    event->kind = kind;
}

/* A C0 control or DEL as a key (R105): the legacy pairs are the named
 * keys, NUL is Ctrl+Space, the rest are Ctrl + the character 0x40 above
 * them, lower-cased for letters. */
static void control_key(OvdEvent *event, unsigned char byte, uint8_t modifiers)
{
    switch (byte) {
    case 0x0d:
        key(event, OVD_KEY_ENTER, 0, modifiers);
        return;
    case 0x09:
        key(event, OVD_KEY_TAB, 0, modifiers);
        return;
    case 0x08:
    case 0x7f:
        key(event, OVD_KEY_BACKSPACE, 0, modifiers);
        return;
    case 0x00:
        key(event, OVD_KEY_CHAR, ' ', modifiers | OVD_MODIFIER_CTRL);
        return;
    default:
        break;
    }
    const uint32_t base = byte <= 0x1a ? (uint32_t)'a' - 1 : (uint32_t)'@';
    key(event, OVD_KEY_CHAR, base + byte, modifiers | OVD_MODIFIER_CTRL);
}

static uint8_t alt_bit(bool alt)
{
    return alt ? (uint8_t)OVD_MODIFIER_ALT : 0;
}

/* ---- State ------------------------------------------------------------ */

static void to_ground(OvdDecoder *decoder)
{
    decoder->state = OVD_DECODER_GROUND;
    decoder->params_length = 0;
    decoder->intermediates = false;
    decoder->overflow = false;
}

/* Bytes are held, outside paste, that the escape delay resolves. */
static bool held(const OvdDecoder *decoder)
{
    return decoder->state == OVD_DECODER_ESCAPE || decoder->state == OVD_DECODER_CSI
        || decoder->state == OVD_DECODER_SS3
        || (decoder->state == OVD_DECODER_GROUND && decoder->utf8_length > 0);
}

static uint64_t deadline_of(const OvdDecoder *decoder)
{
    const uint64_t delay = decoder->delay_ms;
    return decoder->last_ms > UINT64_MAX - delay ? UINT64_MAX : decoder->last_ms + delay;
}

/* The held partial scalar, ended early, is one U+FFFD (001 R17). */
static void replaced_key(OvdDecoder *decoder, OvdEvent *event)
{
    key(event, OVD_KEY_CHAR, REPLACEMENT_CODE_POINT, alt_bit(decoder->alt));
    decoder->utf8_length = 0;
    decoder->alt = false;
}

/* Held bytes after the delay (design D3). Returns whether *event is set. */
static bool expire(OvdDecoder *decoder, OvdEvent *event)
{
    bool produced = true;
    switch (decoder->state) {
    case OVD_DECODER_ESCAPE:
        key(event, OVD_KEY_ESCAPE, 0, 0);
        break;
    case OVD_DECODER_CSI:
        produced = decoder->params_length == 0 && !decoder->intermediates && !decoder->overflow;
        if (produced) {
            key(event, OVD_KEY_CHAR, '[', OVD_MODIFIER_ALT);
        }
        break;
    case OVD_DECODER_SS3:
        key(event, OVD_KEY_CHAR, 'O', OVD_MODIFIER_ALT);
        break;
    default:
        replaced_key(decoder, event);
        break;
    }
    to_ground(decoder);
    return produced;
}

/* ---- Ground: text keys and C0 ----------------------------------------- */

/* The next byte of a scalar, with utf8[0..utf8_length) held before it. */
static Step text_byte(OvdDecoder *decoder, unsigned char byte, OvdEvent *event)
{
    unsigned char bytes[4];
    memcpy(bytes, decoder->utf8, decoder->utf8_length);
    bytes[decoder->utf8_length] = byte;
    const size_t available = (size_t)decoder->utf8_length + 1;
    const OvdUtf8Unit unit = ovd_unicode_decode_utf8((const char *)bytes, available);
    switch (unit.status) {
    case OVD_UTF8_SCALAR:
        key(event, OVD_KEY_CHAR, unit.code_point, alt_bit(decoder->alt));
        decoder->utf8_length = 0;
        decoder->alt = false;
        return STEP_EVENT;
    case OVD_UTF8_TRUNCATED:
        decoder->utf8[decoder->utf8_length] = byte;
        decoder->utf8_length += 1;
        return STEP_CONSUMED;
    default:
        break;
    }
    /* The held bytes are a well-formed prefix, so a subpart ends either at
     * this byte (not taken) or, with nothing held, is this byte alone. */
    if (decoder->utf8_length > 0) {
        replaced_key(decoder, event);
        return STEP_EVENT_KEPT;
    }
    key(event, OVD_KEY_CHAR, REPLACEMENT_CODE_POINT, alt_bit(decoder->alt));
    decoder->alt = false;
    return STEP_EVENT;
}

static Step ground_byte(OvdDecoder *decoder, unsigned char byte, OvdEvent *event)
{
    if (decoder->utf8_length > 0) {
        return text_byte(decoder, byte, event);
    }
    if (byte == ESC) {
        decoder->state = OVD_DECODER_ESCAPE;
        return STEP_CONSUMED;
    }
    if (byte < 0x20 || byte == 0x7f) {
        control_key(event, byte, 0);
        return STEP_EVENT;
    }
    return text_byte(decoder, byte, event);
}

/* After ESC: a CSI or SS3 introducer, or Alt + the next key (R105). */
static Step escape_byte(OvdDecoder *decoder, unsigned char byte, OvdEvent *event)
{
    switch (byte) {
    case '[':
        decoder->state = OVD_DECODER_CSI;
        return STEP_CONSUMED;
    case 'O':
        decoder->state = OVD_DECODER_SS3;
        return STEP_CONSUMED;
    case ESC:
        to_ground(decoder);
        key(event, OVD_KEY_ESCAPE, 0, OVD_MODIFIER_ALT);
        return STEP_EVENT;
    default:
        break;
    }
    to_ground(decoder);
    if (byte < 0x20 || byte == 0x7f) {
        control_key(event, byte, OVD_MODIFIER_ALT);
        return STEP_EVENT;
    }
    decoder->alt = true; /* Alt + a text key, possibly a held partial scalar */
    return text_byte(decoder, byte, event);
}

/* ---- Layer 2: the dispatch table (design D4) --------------------------- */

typedef struct Params {
    unsigned char prefix; /* a private marker 0x3C-0x3F, or 0 */
    size_t count;
    uint64_t values[VALUES_MAX];
} Params;

/* Parses the parameter area as [prefix] value (';' value)*: each value
 * non-empty decimal, at most VALUE_MAX, at most VALUES_MAX of them. An
 * empty area has count 0. Anything else fails. */
static bool parse_params(const OvdDecoder *decoder, Params *out)
{
    *out = (Params){ 0 };
    size_t i = 0;
    const size_t length = decoder->params_length;
    if (length > 0 && decoder->params[0] >= 0x3c && decoder->params[0] <= 0x3f) {
        out->prefix = (unsigned char)decoder->params[0];
        i = 1;
    }
    if (i == length) {
        return true;
    }
    for (;;) {
        if (out->count == VALUES_MAX) {
            return false;
        }
        uint64_t value = 0;
        size_t digits = 0;
        while (i < length && decoder->params[i] >= '0' && decoder->params[i] <= '9') {
            value = value * 10 + (uint64_t)(decoder->params[i] - '0');
            if (value > VALUE_MAX) {
                return false; /* stops before value * 10 can wrap */
            }
            ++digits;
            ++i;
        }
        if (digits == 0) {
            return false;
        }
        out->values[out->count++] = value;
        if (i == length) {
            return true;
        }
        if (decoder->params[i] != ';') {
            return false;
        }
        ++i;
    }
}

/* xterm's modifier code m = 1 + (1 Shift, 2 Alt, 4 Control, 8 Meta);
 * Meta is dropped (R105). The low three bits are OvdModifier's values. */
static bool modifiers_of(uint64_t code, uint8_t *modifiers)
{
    if (code < 1 || code > 16) {
        return false;
    }
    *modifiers = (uint8_t)((code - 1) & 7u);
    return true;
}

static bool letter_key(unsigned char final, OvdKey *which)
{
    switch (final) {
    case 'A': *which = OVD_KEY_UP; return true;
    case 'B': *which = OVD_KEY_DOWN; return true;
    case 'C': *which = OVD_KEY_RIGHT; return true;
    case 'D': *which = OVD_KEY_LEFT; return true;
    case 'H': *which = OVD_KEY_HOME; return true;
    case 'F': *which = OVD_KEY_END; return true;
    case 'P': *which = OVD_KEY_F1; return true;
    case 'Q': *which = OVD_KEY_F2; return true;
    case 'R': *which = OVD_KEY_F3; return true;
    case 'S': *which = OVD_KEY_F4; return true;
    default: return false;
    }
}

static bool tilde_key(uint64_t number, OvdKey *which)
{
    switch (number) {
    case 1: case 7: *which = OVD_KEY_HOME; return true;
    case 2: *which = OVD_KEY_INSERT; return true;
    case 3: *which = OVD_KEY_DELETE; return true;
    case 4: case 8: *which = OVD_KEY_END; return true;
    case 5: *which = OVD_KEY_PAGE_UP; return true;
    case 6: *which = OVD_KEY_PAGE_DOWN; return true;
    case 15: *which = OVD_KEY_F5; return true;
    case 17: *which = OVD_KEY_F6; return true;
    case 18: *which = OVD_KEY_F7; return true;
    case 19: *which = OVD_KEY_F8; return true;
    case 20: *which = OVD_KEY_F9; return true;
    case 21: *which = OVD_KEY_F10; return true;
    case 23: *which = OVD_KEY_F11; return true;
    case 24: *which = OVD_KEY_F12; return true;
    default: return false;
    }
}

/* SGR mouse, CSI < b ; x ; y M or m (xterm SGR (1006), Mouse Tracking). */
static bool mouse(const Params *params, unsigned char final, OvdEvent *event)
{
    if (params->count != 3 || (final != 'M' && final != 'm')) {
        return false;
    }
    const uint64_t code = params->values[0];
    const uint64_t x = params->values[1];
    const uint64_t y = params->values[2];
    if (code >= 128 || x == 0 || y == 0) {
        return false; /* buttons 8-11, and coordinates are 1-based */
    }
    const bool press = final == 'M';
    const unsigned low = (unsigned)(code & 3u);
    OvdMouseAction action;
    OvdMouseButton button;
    if ((code & 64u) != 0) {
        if (!press || (code & 32u) != 0 || low > 1) {
            return false; /* no wheel release or motion; buttons 6-7 */
        }
        action = OVD_MOUSE_ACTION_WHEEL;
        button = low == 0 ? OVD_MOUSE_BUTTON_WHEEL_UP : OVD_MOUSE_BUTTON_WHEEL_DOWN;
    } else {
        static const OvdMouseButton buttons[4] = { OVD_MOUSE_BUTTON_LEFT, OVD_MOUSE_BUTTON_MIDDLE,
                                                   OVD_MOUSE_BUTTON_RIGHT,
                                                   OVD_MOUSE_BUTTON_NONE };
        button = buttons[low];
        if ((code & 32u) != 0) {
            if (!press) {
                return false;
            }
            action = OVD_MOUSE_ACTION_DRAG;
        } else if (press) {
            if (low == 3) {
                return false; /* a press names its button */
            }
            action = OVD_MOUSE_ACTION_PRESS;
        } else {
            action = OVD_MOUSE_ACTION_RELEASE;
        }
    }
    uint8_t modifiers = 0;
    if ((code & 4u) != 0) {
        modifiers |= OVD_MODIFIER_SHIFT;
    }
    if ((code & 8u) != 0) {
        modifiers |= OVD_MODIFIER_ALT; /* xterm's "Meta" is the Alt key's modifier */
    }
    if ((code & 16u) != 0) {
        modifiers |= OVD_MODIFIER_CTRL;
    }
    clear(event);
    event->kind = OVD_EVENT_KIND_MOUSE;
    event->mouse.action = action;
    event->mouse.button = button;
    event->mouse.x = (int32_t)(x - 1);
    event->mouse.y = (int32_t)(y - 1);
    event->mouse.modifiers = modifiers;
    return true;
}

/* A complete CSI without intermediates. Returns whether *event is set;
 * sets *paste for CSI 200 ~. */
static bool dispatch_csi(const OvdDecoder *decoder, unsigned char final, OvdEvent *event,
                         bool *paste)
{
    Params params;
    if (!parse_params(decoder, &params)) {
        return false;
    }
    if (params.prefix == '<') {
        return mouse(&params, final, event);
    }
    if (params.prefix != 0) {
        return false;
    }
    OvdKey which;
    uint8_t modifiers = 0;
    if (letter_key(final, &which)) {
        if (params.count == 0
            || (params.count == 2 && params.values[0] == 1
                && modifiers_of(params.values[1], &modifiers))) {
            key(event, which, 0, modifiers);
            return true;
        }
        return false;
    }
    if (params.count == 0) {
        switch (final) {
        case 'Z':
            key(event, OVD_KEY_TAB, 0, OVD_MODIFIER_SHIFT);
            return true;
        case 'I':
            simple(event, OVD_EVENT_KIND_FOCUS_IN);
            return true;
        case 'O':
            simple(event, OVD_EVENT_KIND_FOCUS_OUT);
            return true;
        default:
            return false;
        }
    }
    if (final != '~') {
        return false;
    }
    if (params.count == 1 && params.values[0] == 200) {
        simple(event, OVD_EVENT_KIND_PASTE_START);
        *paste = true;
        return true;
    }
    if (params.count > 2 || !tilde_key(params.values[0], &which)) {
        return false;
    }
    if (params.count == 2 && !modifiers_of(params.values[1], &modifiers)) {
        return false;
    }
    key(event, which, 0, modifiers);
    return true;
}

/* ---- Layer 1: CSI and SS3 syntax --------------------------------------- */

static Step csi_byte(OvdDecoder *decoder, unsigned char byte, OvdEvent *event)
{
    if (byte >= 0x30 && byte <= 0x3f) {
        if (decoder->intermediates || decoder->params_length == OVD_DECODER_PARAMS_MAX) {
            decoder->overflow = true; /* consumed, not stored */
        } else {
            decoder->params[decoder->params_length++] = (char)byte;
        }
        return STEP_CONSUMED;
    }
    if (byte >= 0x20 && byte <= 0x2f) {
        decoder->intermediates = true;
        return STEP_CONSUMED;
    }
    if (byte >= 0x40 && byte <= 0x7e) {
        bool paste = false;
        const bool produced = !decoder->intermediates && !decoder->overflow
            && dispatch_csi(decoder, byte, event, &paste);
        to_ground(decoder);
        if (paste) {
            decoder->state = OVD_DECODER_PASTE;
            decoder->marker_length = 0;
        }
        return produced ? STEP_EVENT : STEP_CONSUMED;
    }
    to_ground(decoder); /* outside the grammar: discard, decode afresh */
    return STEP_AGAIN;
}

static Step ss3_byte(OvdDecoder *decoder, unsigned char byte, OvdEvent *event)
{
    to_ground(decoder);
    if (byte < 0x40 || byte > 0x7e) {
        return STEP_AGAIN;
    }
    OvdKey which;
    if (!letter_key(byte, &which)) {
        return STEP_CONSUMED;
    }
    key(event, which, 0, 0);
    return STEP_EVENT;
}

/* ---- Paste (R103, design D5) ------------------------------------------ */

typedef struct Chunk {
    char *bytes;
    size_t length;
    size_t capacity;
} Chunk;

static void append(Chunk *chunk, const void *bytes, size_t size)
{
    memcpy(chunk->bytes + chunk->length, bytes, size);
    chunk->length += size;
}

/* One paste byte into the chunk. The caller has checked that at least
 * OVD_DECODER_TEXT_MIN bytes of room remain. */
static Step paste_byte(OvdDecoder *decoder, unsigned char byte, Chunk *chunk, OvdEvent *event)
{
    if (decoder->marker_length > 0 || byte == ESC) {
        if (decoder->marker_length == 0 && decoder->utf8_length > 0) {
            append(chunk, replacement, sizeof replacement - 1); /* ESC ends the scalar */
            decoder->utf8_length = 0;
        }
        if (byte == (unsigned char)paste_end[decoder->marker_length]) {
            if (decoder->marker_length + 1 < PASTE_END_LENGTH) {
                decoder->marker_length += 1;
                return STEP_CONSUMED;
            }
            if (chunk->length > 0) {
                return STEP_EVENT_KEPT; /* the text first; ~ ends it next call */
            }
            decoder->marker_length = 0;
            to_ground(decoder);
            simple(event, OVD_EVENT_KIND_PASTE_END);
            return STEP_EVENT;
        }
        append(chunk, paste_end, decoder->marker_length); /* not the marker: text */
        decoder->marker_length = 0;
        return STEP_AGAIN;
    }
    unsigned char bytes[4];
    memcpy(bytes, decoder->utf8, decoder->utf8_length);
    bytes[decoder->utf8_length] = byte;
    const size_t available = (size_t)decoder->utf8_length + 1;
    const OvdUtf8Unit unit = ovd_unicode_decode_utf8((const char *)bytes, available);
    switch (unit.status) {
    case OVD_UTF8_SCALAR:
        append(chunk, bytes, available);
        decoder->utf8_length = 0;
        return STEP_CONSUMED;
    case OVD_UTF8_TRUNCATED:
        decoder->utf8[decoder->utf8_length] = byte;
        decoder->utf8_length += 1;
        return STEP_CONSUMED;
    default:
        break;
    }
    append(chunk, replacement, sizeof replacement - 1);
    if (decoder->utf8_length > 0) {
        decoder->utf8_length = 0;
        return STEP_AGAIN; /* this byte ended the subpart */
    }
    return STEP_CONSUMED;
}

static void text_event(const Chunk *chunk, OvdEvent *event)
{
    clear(event);
    event->kind = OVD_EVENT_KIND_PASTE_TEXT;
    event->paste.bytes = chunk->bytes;
    event->paste.length = chunk->length;
}

/* ---- Interface -------------------------------------------------------- */

bool ovd_decoder_init(OvdDecoder *decoder, uint32_t escape_delay_ms, char *text,
                      size_t text_capacity)
{
    if (decoder == nullptr || text == nullptr || text_capacity < OVD_DECODER_TEXT_MIN) {
        return false;
    }
    memset(decoder, 0, sizeof *decoder);
    decoder->text = text;
    decoder->text_capacity = text_capacity;
    decoder->delay_ms = escape_delay_ms;
    decoder->state = OVD_DECODER_GROUND;
    return true;
}

bool ovd_decoder_next(OvdDecoder *decoder, const char *bytes, size_t length, uint64_t now_ms,
                      size_t *consumed, OvdEvent *event)
{
    if (decoder == nullptr || consumed == nullptr || event == nullptr
        || (bytes == nullptr && length > 0)) {
        return false;
    }
    *consumed = 0;
    clear(event);
    /* The deadline is never before last_ms, so a clock that ran backwards
     * expires nothing. */
    if (held(decoder) && now_ms >= deadline_of(decoder) && expire(decoder, event)) {
        return true;
    }

    Chunk chunk = { .bytes = decoder->text, .length = 0, .capacity = decoder->text_capacity };
    size_t offset = 0;
    bool produced = false;
    while (offset < length && !produced) {
        const unsigned char byte = (unsigned char)bytes[offset];
        Step step;
        switch (decoder->state) {
        case OVD_DECODER_ESCAPE:
            step = escape_byte(decoder, byte, event);
            break;
        case OVD_DECODER_CSI:
            step = csi_byte(decoder, byte, event);
            break;
        case OVD_DECODER_SS3:
            step = ss3_byte(decoder, byte, event);
            break;
        case OVD_DECODER_PASTE:
            if (chunk.length > 0 && chunk.capacity - chunk.length < OVD_DECODER_TEXT_MIN) {
                text_event(&chunk, event);
                step = STEP_EVENT_KEPT; /* full: this byte goes in the next chunk */
                break;
            }
            step = paste_byte(decoder, byte, &chunk, event);
            if (step == STEP_EVENT_KEPT) {
                text_event(&chunk, event);
            }
            break;
        default:
            step = ground_byte(decoder, byte, event);
            break;
        }
        switch (step) {
        case STEP_CONSUMED:
            ++offset;
            break;
        case STEP_EVENT:
            ++offset;
            produced = true;
            break;
        case STEP_EVENT_KEPT:
            produced = true;
            break;
        case STEP_AGAIN:
            break;
        }
    }
    if (!produced && chunk.length > 0) {
        text_event(&chunk, event);
        produced = true;
    }
    if (offset > 0) {
        decoder->last_ms = now_ms;
    }
    *consumed = offset;
    return produced;
}

bool ovd_decoder_deadline(const OvdDecoder *decoder, uint64_t *deadline_ms)
{
    if (decoder == nullptr || deadline_ms == nullptr || !held(decoder)) {
        return false;
    }
    *deadline_ms = deadline_of(decoder);
    return true;
}
