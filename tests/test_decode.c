/* The decoder's SC-001 table (T105 to T107): one row per sequence, in
 * readable notation (Art. VI, R117). Each row is decoded whole, split in
 * two at every byte boundary, and fed one byte per call; all three must
 * give the same events (R104).
 *
 * Sources of the sequence forms (quoted in the PR): xterm "Control
 * Sequences" (Patch #411), sections PC-Style Function Keys, VT220-Style
 * Function Keys, Mouse Tracking, SGR (1006), Bracketed Paste Mode and
 * FocusIn/FocusOut; Microsoft "Console Virtual Terminal Sequences",
 * section Input Sequences. */
#include "decode.h"
#include "unicode.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

constexpr uint32_t DELAY_MS = 50;
constexpr uint64_t AFTER_DELAY_MS = 1000;
constexpr size_t LOG_CAPACITY = 8192;

/* ---- Readable events (Art. VI) --------------------------------------- */

typedef struct Log {
    char text[LOG_CAPACITY];
    size_t length;
    bool text_open; /* the last item is a paste text, merged with the next */
    bool overflowed;
} Log;

static void put(Log *log, const char *text)
{
    const size_t size = strlen(text);
    if (log->length + size >= LOG_CAPACITY) {
        log->overflowed = true;
        return;
    }
    memcpy(log->text + log->length, text, size + 1);
    log->length += size;
}

static void put_separator(Log *log)
{
    if (log->length > 0) {
        put(log, ", ");
    }
}

static const char *key_name(OvdKey key)
{
    static const char *const names[] = {
        [OVD_KEY_ENTER] = "Enter",      [OVD_KEY_TAB] = "Tab",
        [OVD_KEY_BACKSPACE] = "Backspace", [OVD_KEY_ESCAPE] = "Escape",
        [OVD_KEY_UP] = "Up",            [OVD_KEY_DOWN] = "Down",
        [OVD_KEY_LEFT] = "Left",        [OVD_KEY_RIGHT] = "Right",
        [OVD_KEY_HOME] = "Home",        [OVD_KEY_END] = "End",
        [OVD_KEY_PAGE_UP] = "PageUp",   [OVD_KEY_PAGE_DOWN] = "PageDown",
        [OVD_KEY_INSERT] = "Insert",    [OVD_KEY_DELETE] = "Delete",
        [OVD_KEY_F1] = "F1",            [OVD_KEY_F2] = "F2",
        [OVD_KEY_F3] = "F3",            [OVD_KEY_F4] = "F4",
        [OVD_KEY_F5] = "F5",            [OVD_KEY_F6] = "F6",
        [OVD_KEY_F7] = "F7",            [OVD_KEY_F8] = "F8",
        [OVD_KEY_F9] = "F9",            [OVD_KEY_F10] = "F10",
        [OVD_KEY_F11] = "F11",          [OVD_KEY_F12] = "F12",
    };
    if ((size_t)key >= sizeof names / sizeof names[0] || names[key] == nullptr) {
        return "?";
    }
    return names[key];
}

static void put_modifiers(Log *log, uint8_t modifiers)
{
    if ((modifiers & OVD_MODIFIER_SHIFT) != 0) {
        put(log, " +shift");
    }
    if ((modifiers & OVD_MODIFIER_ALT) != 0) {
        put(log, " +alt");
    }
    if ((modifiers & OVD_MODIFIER_CTRL) != 0) {
        put(log, " +ctrl");
    }
    if ((modifiers & ~7u) != 0) {
        put(log, " +?");
    }
}

/* Paste bytes as a C-like string: ESC is \e, other controls and every
 * non-ASCII byte \xNN. */
static void put_text(Log *log, const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        const unsigned char byte = (unsigned char)bytes[i];
        char piece[8];
        if (byte == 0x1b) {
            snprintf(piece, sizeof piece, "\\e");
        } else if (byte < 0x20 || byte >= 0x7f) {
            snprintf(piece, sizeof piece, "\\x%02x", byte);
        } else if (byte == '"' || byte == '\\') {
            snprintf(piece, sizeof piece, "\\%c", byte);
        } else {
            snprintf(piece, sizeof piece, "%c", byte);
        }
        put(log, piece);
    }
}

static void log_event(Log *log, const OvdEvent *event)
{
    static const char *const actions[] = { "press", "release", "drag", "wheel" };
    static const char *const buttons[] = { "none", "left", "middle", "right", "up", "down" };
    char piece[96];
    if (event->kind == OVD_EVENT_KIND_PASTE_TEXT && log->text_open) {
        log->length -= 1; /* reopen the quoted text */
        log->text[log->length] = '\0';
        put_text(log, event->paste.bytes, event->paste.length);
        put(log, "\"");
        return;
    }
    put_separator(log);
    log->text_open = false;
    switch (event->kind) {
    case OVD_EVENT_KIND_KEY:
        if (event->key.key != OVD_KEY_CHAR) {
            snprintf(piece, sizeof piece, "key %s", key_name(event->key.key));
        } else if (event->key.codepoint >= 0x20 && event->key.codepoint < 0x7f) {
            snprintf(piece, sizeof piece, "key '%c'", (char)event->key.codepoint);
        } else {
            snprintf(piece, sizeof piece, "key U+%04" PRIX32, event->key.codepoint);
        }
        put(log, piece);
        put_modifiers(log, event->key.modifiers);
        break;
    case OVD_EVENT_KIND_MOUSE:
        snprintf(piece, sizeof piece, "mouse %s %s %" PRId32 ",%" PRId32,
                 event->mouse.action < 4 ? actions[event->mouse.action] : "?",
                 event->mouse.button < 6 ? buttons[event->mouse.button] : "?", event->mouse.x,
                 event->mouse.y);
        put(log, piece);
        put_modifiers(log, event->mouse.modifiers);
        break;
    case OVD_EVENT_KIND_PASTE_START:
        put(log, "paste start");
        break;
    case OVD_EVENT_KIND_PASTE_TEXT:
        put(log, "paste \"");
        put_text(log, event->paste.bytes, event->paste.length);
        put(log, "\"");
        log->text_open = true;
        break;
    case OVD_EVENT_KIND_PASTE_END:
        put(log, "paste end");
        break;
    case OVD_EVENT_KIND_FOCUS_IN:
        put(log, "focus in");
        break;
    case OVD_EVENT_KIND_FOCUS_OUT:
        put(log, "focus out");
        break;
    default:
        put(log, "?");
        break;
    }
}

/* ---- Driving the decoder --------------------------------------------- */

static bool valid_utf8(const char *bytes, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        const OvdUtf8Unit unit = ovd_unicode_decode_utf8(bytes + offset, length - offset);
        if (unit.status != OVD_UTF8_SCALAR) {
            return false;
        }
        offset += unit.size;
    }
    return true;
}

/* A paste chunk is valid UTF-8 in the chunk buffer; a key or mouse event
 * never comes from paste state (data-model invariants). */
static bool event_valid(const OvdDecoder *decoder, OvdDecoderState before, const OvdEvent *event)
{
    if (before == OVD_DECODER_PASTE) {
        CHECK(event->kind == OVD_EVENT_KIND_PASTE_TEXT || event->kind == OVD_EVENT_KIND_PASTE_END);
    }
    if (event->kind == OVD_EVENT_KIND_PASTE_TEXT) {
        CHECK(event->paste.bytes == decoder->text);
        CHECK(event->paste.length > 0 && event->paste.length <= decoder->text_capacity);
        CHECK(valid_utf8(event->paste.bytes, event->paste.length));
    }
    if (event->kind == OVD_EVENT_KIND_KEY && event->key.key == OVD_KEY_CHAR) {
        CHECK(event->key.codepoint <= 0x10FFFF);
        CHECK(event->key.codepoint < 0xD800 || event->key.codepoint > 0xDFFF);
    } else if (event->kind == OVD_EVENT_KIND_KEY) {
        CHECK(event->key.codepoint == 0);
    }
    return true;
}

/* Feeds bytes[0..length) at now_ms, calling the decoder until it has
 * consumed every byte and returns no event, and logs each event. */
static bool feed(OvdDecoder *decoder, const char *bytes, size_t length, uint64_t now_ms, Log *log)
{
    size_t offset = 0;
    for (size_t calls = 0; calls <= 4 * length + 8; ++calls) {
        const OvdDecoderState before = decoder->state;
        size_t consumed = SIZE_MAX;
        OvdEvent event;
        memset(&event, 0xA5, sizeof event);
        const bool produced = ovd_decoder_next(decoder, bytes == nullptr ? nullptr : bytes + offset,
                                               length - offset, now_ms, &consumed, &event);
        CHECK(consumed <= length - offset);
        CHECK(produced || consumed > 0 || offset == length);
        offset += consumed;
        if (produced) {
            CHECK(event_valid(decoder, before, &event));
            log_event(log, &event);
        } else if (offset == length) {
            return true;
        }
    }
    fprintf(stderr, "%s:%d: no progress\n", __FILE__, __LINE__);
    return false;
}

static bool fresh(OvdDecoder *decoder, char *text, size_t capacity)
{
    return ovd_decoder_init(decoder, DELAY_MS, text, capacity);
}

/* Decodes the row in pieces: whole when cut is 0 and not bytewise, else
 * split at cut, or one byte per call. Then lets the delay pass. */
static bool decode_pieces(const char *in, size_t length, size_t cut, bool bytewise, Log *log)
{
    OvdDecoder decoder;
    char text[64];
    CHECK(fresh(&decoder, text, sizeof text));
    memset(log, 0, sizeof *log);
    if (bytewise) {
        for (size_t i = 0; i < length; ++i) {
            CHECK(feed(&decoder, in + i, 1, 0, log));
        }
    } else {
        CHECK(feed(&decoder, in, cut, 0, log));
        CHECK(feed(&decoder, in + cut, length - cut, 0, log));
    }
    CHECK(feed(&decoder, nullptr, 0, AFTER_DELAY_MS, log));
    CHECK(!log->overflowed);
    return true;
}

static bool expect(const char *in, size_t length, const char *expected, int line)
{
    /* The input as readable text, bounded like any log (put refuses past
     * the capacity). */
    static Log shown_log;
    memset(&shown_log, 0, sizeof shown_log);
    put_text(&shown_log, in, length);
    const char *shown = shown_log.text;
    Log log;
    for (size_t pass = 0; pass <= length + 1; ++pass) {
        /* pass 0: whole; 1..length-1: split there; length+1: bytewise */
        if (pass == length && length > 0) {
            continue;
        }
        const bool bytewise = pass == length + 1;
        const size_t cut = bytewise ? 0 : (pass == 0 ? length : pass);
        if (!decode_pieces(in, length, cut, bytewise, &log)) {
            fprintf(stderr, "%s:%d: \"%s\" failed to decode\n", __FILE__, line, shown);
            return false;
        }
        if (strcmp(log.text, expected) != 0) {
            fprintf(stderr, "%s:%d: \"%s\" (%s) -> \"%s\", expected \"%s\"\n", __FILE__, line,
                    shown,
                    bytewise ? "bytewise" : (pass == 0 ? "whole" : "split"), log.text, expected);
            return false;
        }
    }
    return true;
}

typedef struct Row {
    const char *in;
    size_t length;
    const char *out;
    int line;
} Row;

/* sizeof keeps embedded NUL bytes (Ctrl+Space). */
#define ROW(in, out) { (in), sizeof(in) - 1, (out), __LINE__ }

static bool rows_pass(const Row *rows, size_t count)
{
    bool ok = true;
    for (size_t i = 0; i < count; ++i) {
        ok = expect(rows[i].in, rows[i].length, rows[i].out, rows[i].line) && ok;
    }
    return ok;
}

#define ROWS_PASS(rows) rows_pass((rows), sizeof(rows) / sizeof((rows)[0]))

/* ---- Keys (T105) ------------------------------------------------------ */

/* xterm PC-Style Function Keys: cursor keys Normal (CSI) and Application
 * (SS3) forms, Home and End as cursor keys; the VT220 6-key editing
 * keypad; F1-F4 as SS3 P-S, F5-F12 as CSI n ~. Microsoft Input Sequences
 * lists the same Normal/Application cursor forms and editing and
 * function keys, Backspace as 0x7f, and Escape as 0x1b. */
static bool test_keys(void)
{
    static const Row rows[] = {
        ROW("\x1b[A", "key Up"),          ROW("\x1b[B", "key Down"),
        ROW("\x1b[C", "key Right"),       ROW("\x1b[D", "key Left"),
        ROW("\x1b[H", "key Home"),        ROW("\x1b[F", "key End"),
        ROW("\x1bOA", "key Up"),          ROW("\x1bOB", "key Down"),
        ROW("\x1bOC", "key Right"),       ROW("\x1bOD", "key Left"),
        ROW("\x1bOH", "key Home"),        ROW("\x1bOF", "key End"),
        ROW("\x1bOP", "key F1"),          ROW("\x1bOQ", "key F2"),
        ROW("\x1bOR", "key F3"),          ROW("\x1bOS", "key F4"),
        ROW("\x1b[2~", "key Insert"),     ROW("\x1b[3~", "key Delete"),
        ROW("\x1b[1~", "key Home"),       ROW("\x1b[4~", "key End"),
        ROW("\x1b[7~", "key Home"),       ROW("\x1b[8~", "key End"),
        ROW("\x1b[5~", "key PageUp"),     ROW("\x1b[6~", "key PageDown"),
        ROW("\x1b[15~", "key F5"),        ROW("\x1b[17~", "key F6"),
        ROW("\x1b[18~", "key F7"),        ROW("\x1b[19~", "key F8"),
        ROW("\x1b[20~", "key F9"),        ROW("\x1b[21~", "key F10"),
        ROW("\x1b[23~", "key F11"),       ROW("\x1b[24~", "key F12"),
        /* Shift+Tab: xterm "shift-Tab sends ... rather than CSI Z" */
        ROW("\t", "key Tab"),             ROW("\x1b[Z", "key Tab +shift"),
        ROW("\x7f", "key Backspace"),     ROW("\x08", "key Backspace"),
        ROW("\r", "key Enter"),           ROW("\x1b", "key Escape"),
        /* the modifier forms that carry no modifier */
        ROW("\x1b[1;1A", "key Up"),       ROW("\x1b[2;1~", "key Insert"),
        /* Meta (code 9) is outside the modifier set and dropped */
        ROW("\x1b[1;9A", "key Up"),       ROW("\x1b[1;16A", "key Up +shift +alt +ctrl"),
        /* Microsoft: Ctrl + arrows are CSI 1 ; 5 A-D in any mode */
        ROW("\x1b[1;5A", "key Up +ctrl"), ROW("\x1b[1;5D", "key Left +ctrl"),
        /* xterm: "shift-F5 would be sent as CSI 1 5 ; 2 ~" */
        ROW("\x1b[15;2~", "key F5 +shift"),
    };
    return ROWS_PASS(rows);
}

/* xterm's modifier code table, codes 2 to 16: Shift, Alt, Control, and
 * Meta (dropped). */
static const char *const modifier_text[17] = {
    [2] = " +shift",       [3] = " +alt",         [4] = " +shift +alt",
    [5] = " +ctrl",        [6] = " +shift +ctrl", [7] = " +alt +ctrl",
    [8] = " +shift +alt +ctrl", [9] = "",          [10] = " +shift",
    [11] = " +alt",        [12] = " +shift +alt", [13] = " +ctrl",
    [14] = " +shift +ctrl", [15] = " +alt +ctrl", [16] = " +shift +alt +ctrl",
};

/* Every key of US2 scenario 2 in its modifier form, every code 2-16:
 * CSI 1 ; m X for the cursor keys and F1-F4 ("the SS3 sent before F1
 * through F4 is altered to CSI when sending a function key modifier"),
 * CSI n ; m ~ for the editing keypad and F5-F12. */
static bool test_modifier_forms(void)
{
    static const struct {
        const char *form; /* printf form with the code as %d */
        const char *name;
    } forms[] = {
        { "\x1b[1;%dA", "Up" },     { "\x1b[1;%dB", "Down" },   { "\x1b[1;%dC", "Right" },
        { "\x1b[1;%dD", "Left" },   { "\x1b[1;%dH", "Home" },   { "\x1b[1;%dF", "End" },
        { "\x1b[1;%dP", "F1" },     { "\x1b[1;%dQ", "F2" },     { "\x1b[1;%dR", "F3" },
        { "\x1b[1;%dS", "F4" },     { "\x1b[2;%d~", "Insert" }, { "\x1b[3;%d~", "Delete" },
        { "\x1b[1;%d~", "Home" },   { "\x1b[4;%d~", "End" },    { "\x1b[5;%d~", "PageUp" },
        { "\x1b[6;%d~", "PageDown" }, { "\x1b[15;%d~", "F5" },  { "\x1b[17;%d~", "F6" },
        { "\x1b[18;%d~", "F7" },    { "\x1b[19;%d~", "F8" },    { "\x1b[20;%d~", "F9" },
        { "\x1b[21;%d~", "F10" },   { "\x1b[23;%d~", "F11" },   { "\x1b[24;%d~", "F12" },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof forms / sizeof forms[0]; ++i) {
        for (int code = 2; code <= 16; ++code) {
            char in[32];
            char out[64];
            const int length = snprintf(in, sizeof in, forms[i].form, code);
            snprintf(out, sizeof out, "key %s%s", forms[i].name, modifier_text[code]);
            ok = expect(in, (size_t)length, out, __LINE__) && ok;
        }
    }
    return ok;
}

/* C0 keys (R105). Microsoft: "Ctrl+@ (0x40) becomes NUL (0x00), Ctrl+[
 * (0x5b) becomes ESC (0x1b)", "Ctrl + Space | 0x00 (NUL)". The legacy
 * pairs (Tab, Enter, Backspace, Escape) are the named keys above. */
static bool test_control_keys(void)
{
    static const Row rows[] = {
        ROW("\0", "key ' ' +ctrl"),   ROW("\x03", "key 'c' +ctrl"),
        ROW("\n", "key 'j' +ctrl"),   ROW("\x13", "key 's' +ctrl"),
        ROW("\x11", "key 'q' +ctrl"), ROW("\x1a", "key 'z' +ctrl"),
        ROW("\x1c", "key '\\' +ctrl"), ROW("\x1d", "key ']' +ctrl"),
        ROW("\x1e", "key '^' +ctrl"), ROW("\x1f", "key '_' +ctrl"),
    };
    bool ok = ROWS_PASS(rows);
    for (char c = 0x01; c <= 0x1a; ++c) {
        if (c == 0x08 || c == 0x09 || c == 0x0d) {
            continue; /* Backspace, Tab, Enter */
        }
        char out[32];
        snprintf(out, sizeof out, "key '%c' +ctrl", 'a' + (c - 1));
        ok = expect(&c, 1, out, __LINE__) && ok;
    }
    return ok;
}

/* Text keys are one scalar each; each maximal ill-formed subpart is one
 * U+FFFD (001 R17). */
static bool test_text_keys(void)
{
    static const Row rows[] = {
        ROW("q", "key 'q'"),
        ROW(" ", "key ' '"),
        ROW("Ab", "key 'A', key 'b'"),
        ROW("\xc3\xa9", "key U+00E9"),
        ROW("\xe2\x82\xac", "key U+20AC"),
        ROW("\xf0\x9f\x98\x80", "key U+1F600"),
        ROW("\xc2\x85", "key U+0085"),
        ROW("\xc3" "A", "key U+FFFD, key 'A'"),
        ROW("\xff", "key U+FFFD"),
        ROW("\x80", "key U+FFFD"),
        ROW("\xc0\xaf", "key U+FFFD, key U+FFFD"),
        ROW("\xe0\x80\x80", "key U+FFFD, key U+FFFD, key U+FFFD"),
        ROW("\xed\xa0\x80", "key U+FFFD, key U+FFFD, key U+FFFD"),
        ROW("\xf4\x90\x80\x80", "key U+FFFD, key U+FFFD, key U+FFFD, key U+FFFD"),
        ROW("\xf0\x9f\x98", "key U+FFFD"),
        ROW("\xf0\x9f\x98" "\x1b[A", "key U+FFFD, key Up"),
        ROW("\xe2\x82" "\r", "key U+FFFD, key Enter"),
    };
    return ROWS_PASS(rows);
}

/* Microsoft: "Alt is treated by prefixing the sequence with an escape:
 * ESC <c>" and "Alt+Ctrl is handled the same way except that ... <c>
 * [is] the appropriate control character". R105: ESC [ and ESC O alone
 * are Alt+[ and Alt+O once the delay passes. */
static bool test_alt_keys(void)
{
    static const Row rows[] = {
        ROW("\x1b" "a", "key 'a' +alt"),
        ROW("\x1b" "A", "key 'A' +alt"),
        ROW("\x1b\r", "key Enter +alt"),
        ROW("\x1b\t", "key Tab +alt"),
        ROW("\x1b\x7f", "key Backspace +alt"),
        ROW("\x1b\x01", "key 'a' +alt +ctrl"),
        ROW("\x1b\x1b", "key Escape +alt"),
        ROW("\x1b\xc3\xa9", "key U+00E9 +alt"),
        ROW("\x1b\xc3", "key U+FFFD +alt"),
        ROW("\x1b\xc3" "A", "key U+FFFD +alt, key 'A'"),
        ROW("\x1b\xff", "key U+FFFD +alt"),
        ROW("\x1b[", "key '[' +alt"),
        ROW("\x1bO", "key 'O' +alt"),
        ROW("\x1b\x1b[A", "key Escape +alt, key '[', key 'A'"),
    };
    return ROWS_PASS(rows);
}

/* FR-014: unknown or malformed sequences are discarded whole, and the
 * key after each one ('z') still arrives. */
static bool test_discarded(void)
{
    static const Row rows[] = {
        ROW("\x1b[1;2;3x" "z", "key 'z'"),      /* unknown final */
        ROW("\x1b[2 q" "z", "key 'z'"),         /* intermediate */
        ROW("\x1b[2 1~" "z", "key 'z'"),        /* parameter after intermediate */
        ROW("\x1b[?1;2c" "z", "key 'z'"),       /* a DA reply */
        ROW("\x1b[?1u" "z", "key 'z'"),         /* a kitty flags reply (R108) */
        ROW("\x1b[97;5u" "z", "key 'z'"),       /* a kitty key report (R108) */
        ROW("\x1b[201~" "z", "key 'z'"),        /* paste end outside a paste */
        ROW("\x1b[200;1~" "z", "key 'z'"),
        ROW("\x1b[1;17A" "z", "key 'z'"),       /* modifier code above 16 */
        ROW("\x1b[1;0A" "z", "key 'z'"),
        ROW("\x1b[;5A" "z", "key 'z'"),         /* empty parameter */
        ROW("\x1b[1:5A" "z", "key 'z'"),        /* sub-parameter */
        ROW("\x1b[5A" "z", "key 'z'"),          /* CUU shape, not a key */
        ROW("\x1b[2;5;1~" "z", "key 'z'"),
        ROW("\x1b[9~" "z", "key 'z'"),
        ROW("\x1b[1;5Z" "z", "key 'z'"),
        ROW("\x1b[1I" "z", "key 'z'"),
        ROW("\x1bOX" "z", "key 'z'"),
        ROW("\x1bO1" "z", "key '1', key 'z'"), /* SS3 takes only a final byte: 1 ends it */
        ROW("\x1b[1;5", ""),                    /* unfinished, delay passes */
        ROW("\x1b[1", ""),                      /* one parameter byte: not Alt+[ */
        ROW("\x1b[ ", ""),                      /* an intermediate alone: not Alt+[ */
        ROW("\x1b[ A" "z", "key 'z'"),          /* intermediate before a known final */
        ROW("\x1b[1;5 C" "z", "key 'z'"),
        /* a byte outside the CSI grammar ends it and is decoded afresh */
        ROW("\x1b[1\x01", "key 'a' +ctrl"),
        ROW("\x1b[1;5\x1b[B", "key Down"),
        ROW("\x1b[1\xc3\xa9", "key U+00E9"),
        ROW("\x1bO\x01", "key 'a' +ctrl"),
    };
    return ROWS_PASS(rows);
}

/* The 64-byte parameter area (R104): exactly 64 bytes still decode; one
 * more and the sequence is discarded to its final byte. */
static bool test_parameter_bound(void)
{
    char in[512];
    size_t length = 0;
    in[length++] = '\x1b';
    in[length++] = '[';
    in[length++] = '1';
    in[length++] = ';';
    for (size_t i = 0; i < OVD_DECODER_PARAMS_MAX - 3; ++i) {
        in[length++] = '0';
    }
    in[length++] = '5';
    in[length++] = 'A';
    CHECK(expect(in, length, "key Up +ctrl", __LINE__));

    length = 4;
    for (size_t i = 0; i < OVD_DECODER_PARAMS_MAX - 2; ++i) {
        in[length++] = '0';
    }
    in[length++] = '5';
    in[length++] = 'A';
    in[length++] = 'z';
    CHECK(expect(in, length, "key 'z'", __LINE__));

    length = 2;
    for (size_t i = 0; i < 400; ++i) {
        in[length++] = (i % 7 == 6) ? ';' : '9';
    }
    in[length++] = 'A';
    in[length++] = 'z';
    CHECK(expect(in, length, "key 'z'", __LINE__));
    return true;
}

/* ---- Mouse (T106) ---------------------------------------------------- */

/* xterm SGR (1006): "CSI < followed by semicolon-separated encoded button
 * value, Px and Py ordinates and a final character which is M for button
 * press and m for button release". Button encoding (Mouse Tracking):
 * low two bits 0=MB1 1=MB2 2=MB3 3=release; 4=Shift, 8=Meta, 16=Control;
 * Button-event tracking "adds 32" for motion; wheel buttons 4 and 5 add
 * 64; buttons 6-11 add 64 or 128. */
static bool test_mouse(void)
{
    static const Row rows[] = {
        ROW("\x1b[<0;1;1M", "mouse press left 0,0"),
        ROW("\x1b[<1;5;3M", "mouse press middle 4,2"),
        ROW("\x1b[<2;80;24M", "mouse press right 79,23"),
        ROW("\x1b[<0;1;1m", "mouse release left 0,0"),
        ROW("\x1b[<2;7;9m", "mouse release right 6,8"),
        ROW("\x1b[<3;1;1m", "mouse release none 0,0"),
        ROW("\x1b[<32;10;5M", "mouse drag left 9,4"),
        ROW("\x1b[<34;10;5M", "mouse drag right 9,4"),
        ROW("\x1b[<35;10;5M", "mouse drag none 9,4"),
        ROW("\x1b[<64;3;4M", "mouse wheel up 2,3"),
        ROW("\x1b[<65;3;4M", "mouse wheel down 2,3"),
        ROW("\x1b[<4;1;1M", "mouse press left 0,0 +shift"),
        ROW("\x1b[<8;1;1M", "mouse press left 0,0 +alt"),
        ROW("\x1b[<16;1;1M", "mouse press left 0,0 +ctrl"),
        ROW("\x1b[<28;1;1M", "mouse press left 0,0 +shift +alt +ctrl"),
        ROW("\x1b[<48;2;2M", "mouse drag left 1,1 +ctrl"),
        ROW("\x1b[<80;1;1M", "mouse wheel up 0,0 +ctrl"),
        /* above X10's 223, and the int32_t limit (design D4) */
        ROW("\x1b[<0;224;300M", "mouse press left 223,299"),
        ROW("\x1b[<0;2147483648;2147483648M", "mouse press left 2147483647,2147483647"),
        /* discarded, and the key after each still arrives */
        ROW("\x1b[<0;2147483649;1M" "z", "key 'z'"),
        ROW("\x1b[<0;1;99999999999999999999M" "z", "key 'z'"),
        ROW("\x1b[<0;0;1M" "z", "key 'z'"),
        ROW("\x1b[<0;1;0M" "z", "key 'z'"),
        ROW("\x1b[<0;1M" "z", "key 'z'"),
        ROW("\x1b[<0;1;1;1M" "z", "key 'z'"),
        ROW("\x1b[<66;1;1M" "z", "key 'z'"),
        ROW("\x1b[<128;1;1M" "z", "key 'z'"),
        ROW("\x1b[<3;1;1M" "z", "key 'z'"),
        ROW("\x1b[<64;1;1m" "z", "key 'z'"),
        ROW("\x1b[<32;1;1m" "z", "key 'z'"),
        ROW("\x1b[0;1;1M" "z", "key 'z'"),
        ROW("\x1b[>0;1;1M" "z", "key 'z'"),
        ROW("\x1b[<0;1;1" "z", ""),   /* z is a final byte: an unknown CSI */
        ROW("\x1b[<0;1;1" "\r", "key Enter"), /* CR is outside: decoded afresh */
    };
    return ROWS_PASS(rows);
}

/* ---- Paste and focus (T107) ------------------------------------------ */

/* xterm Bracketed Paste Mode: "the program will receive: ESC [ 2 0 0 ~ ,
 * followed by the pasted text, followed by ESC [ 2 0 1 ~". FocusIn/
 * FocusOut: "xterm ... send[s] CSI I when the terminal gains focus, and
 * CSI O when it loses focus". */
static bool test_paste_and_focus(void)
{
    static const Row rows[] = {
        ROW("\x1b[200~hello\x1b[201~", "paste start, paste \"hello\", paste end"),
        ROW("\x1b[200~\x1b[201~", "paste start, paste end"),
        ROW("\x1b[200~a\x01\x1b" "b\r\x7f\x1b[201~",
            "paste start, paste \"a\\x01\\eb\\x0d\\x7f\", paste end"),
        /* no key event inside a paste */
        ROW("\x1b[200~\x1b[A\x1b[1;5C\x1b[<0;1;1M\x1b[I\x1b[201~",
            "paste start, paste \"\\e[A\\e[1;5C\\e[<0;1;1M\\e[I\", paste end"),
        ROW("\x1b[200~\x1b[200~\x1b[201~", "paste start, paste \"\\e[200~\", paste end"),
        /* a held marker prefix that does not complete is text */
        ROW("\x1b[200~\x1b[20x\x1b[201~", "paste start, paste \"\\e[20x\", paste end"),
        ROW("\x1b[200~\x1b[201\x1b[201~", "paste start, paste \"\\e[201\", paste end"),
        ROW("\x1b[200~\x1b\x1b[201~", "paste start, paste \"\\e\", paste end"),
        /* repaired UTF-8 (001 R17) */
        ROW("\x1b[200~\xc3(\x1b[201~", "paste start, paste \"\\xef\\xbf\\xbd(\", paste end"),
        ROW("\x1b[200~\xc3\x1b[201~", "paste start, paste \"\\xef\\xbf\\xbd\", paste end"),
        ROW("\x1b[200~\xe2\x82\xac\xf0\x9f\x98\x80\x1b[201~",
            "paste start, paste \"\\xe2\\x82\\xac\\xf0\\x9f\\x98\\x80\", paste end"),
        /* no end marker: the text still arrives; the delay ends nothing */
        ROW("\x1b[200~abc", "paste start, paste \"abc\""),
        ROW("\x1b[200~ab\x1b[201", "paste start, paste \"ab\""),
        ROW("\x1b[200~ab\x1b[201~q", "paste start, paste \"ab\", paste end, key 'q'"),
        ROW("\x1b[I", "focus in"),
        ROW("\x1b[O", "focus out"),
        ROW("\x1b[I\x1b[O", "focus in, focus out"),
    };
    return ROWS_PASS(rows);
}

/* Up to five held-back bytes are never released early or lost (R103). */
static bool test_paste_held_back(void)
{
    OvdDecoder decoder;
    char text[64];
    Log log = { 0 };
    CHECK(fresh(&decoder, text, sizeof text));
    static const char start[] = "\x1b[200~ab\x1b[201";
    CHECK(feed(&decoder, start, sizeof start - 1, 0, &log));
    CHECK(strcmp(log.text, "paste start, paste \"ab\"") == 0);
    CHECK(decoder.marker_length == 5);
    uint64_t deadline = 0;
    CHECK(!ovd_decoder_deadline(&decoder, &deadline)); /* paste is not timed */
    CHECK(feed(&decoder, nullptr, 0, 1000000, &log));
    CHECK(strcmp(log.text, "paste start, paste \"ab\"") == 0);

    memset(&log, 0, sizeof log);
    CHECK(feed(&decoder, "x", 1, 1000000, &log));
    CHECK(strcmp(log.text, "paste \"\\e[201x\"") == 0);
    memset(&log, 0, sizeof log);
    CHECK(feed(&decoder, "\xe2\x82", 2, 1000000, &log)); /* a partial scalar is held too */
    CHECK(strcmp(log.text, "") == 0);
    CHECK(feed(&decoder, "\xac\x1b[201~", 7, 1000000, &log));
    CHECK(strcmp(log.text, "paste \"\\xe2\\x82\\xac\", paste end") == 0);
    CHECK(decoder.state == OVD_DECODER_GROUND);
    return true;
}

/* A chunk never exceeds the buffer, never ends inside a scalar, and the
 * chunks together are the text (FR-016). */
static bool test_paste_chunks(void)
{
    static const char unit[] = "x\xc3\xa9\xf0\x9f\x98\x80\x1b[2";
    char in[2048];
    size_t length = 0;
    memcpy(in, "\x1b[200~", 6);
    length = 6;
    char expected_text[2048];
    size_t expected_length = 0;
    while (length + sizeof unit + 8 < sizeof in) {
        memcpy(in + length, unit, sizeof unit - 1);
        length += sizeof unit - 1;
        memcpy(expected_text + expected_length, unit, sizeof unit - 1);
        expected_length += sizeof unit - 1;
    }
    memcpy(in + length, "\x1b[201~", 6);
    length += 6;

    for (size_t capacity = OVD_DECODER_TEXT_MIN; capacity <= 32; ++capacity) {
        OvdDecoder decoder;
        char text[32];
        CHECK(fresh(&decoder, text, capacity));
        char joined[2048];
        size_t joined_length = 0;
        size_t offset = 0;
        size_t chunks = 0;
        bool ended = false;
        while (!ended) {
            size_t consumed = 0;
            OvdEvent event;
            const bool produced =
                ovd_decoder_next(&decoder, in + offset, length - offset, 0, &consumed, &event);
            CHECK(produced);
            offset += consumed;
            if (event.kind == OVD_EVENT_KIND_PASTE_TEXT) {
                CHECK(event.paste.length <= capacity);
                CHECK(valid_utf8(event.paste.bytes, event.paste.length));
                CHECK(joined_length + event.paste.length <= sizeof joined);
                memcpy(joined + joined_length, event.paste.bytes, event.paste.length);
                joined_length += event.paste.length;
                ++chunks;
            } else if (event.kind == OVD_EVENT_KIND_PASTE_END) {
                ended = true;
            } else {
                CHECK(event.kind == OVD_EVENT_KIND_PASTE_START);
            }
        }
        CHECK(offset == length);
        CHECK(joined_length == expected_length);
        CHECK(memcmp(joined, expected_text, expected_length) == 0);
        CHECK(chunks >= expected_length / capacity);
    }
    return true;
}

/* ---- The escape delay (FR-013, R109, design D3) ---------------------- */

static bool events_are(OvdDecoder *decoder, const char *bytes, size_t length, uint64_t now_ms,
                       const char *expected)
{
    Log log = { 0 };
    CHECK(feed(decoder, bytes, length, now_ms, &log));
    if (strcmp(log.text, expected) != 0) {
        fprintf(stderr, "  got \"%s\", expected \"%s\"\n", log.text, expected);
        return false;
    }
    return true;
}

static bool test_escape_delay(void)
{
    OvdDecoder decoder;
    char text[16];
    uint64_t deadline = 0;

    /* A lone ESC before and after the delay. */
    CHECK(fresh(&decoder, text, sizeof text));
    CHECK(!ovd_decoder_deadline(&decoder, &deadline));
    CHECK(events_are(&decoder, "\x1b", 1, 100, ""));
    CHECK(ovd_decoder_deadline(&decoder, &deadline) && deadline == 150);
    CHECK(events_are(&decoder, nullptr, 0, 149, ""));
    CHECK(events_are(&decoder, nullptr, 0, 150, "key Escape"));
    CHECK(!ovd_decoder_deadline(&decoder, &deadline));

    /* A key within the delay is Alt + key; after it, Escape then the key. */
    CHECK(events_are(&decoder, "\x1b", 1, 0, ""));
    CHECK(events_are(&decoder, "a", 1, 49, "key 'a' +alt"));
    CHECK(events_are(&decoder, "\x1b", 1, 100, ""));
    CHECK(events_are(&decoder, "a", 1, 150, "key Escape, key 'a'"));

    /* A sequence split across reads within the delay is one event; the
     * delay runs from the last byte. */
    CHECK(events_are(&decoder, "\x1b[1", 3, 1000, ""));
    CHECK(events_are(&decoder, ";5", 2, 1040, ""));
    CHECK(ovd_decoder_deadline(&decoder, &deadline) && deadline == 1090);
    CHECK(events_are(&decoder, "C", 1, 1080, "key Right +ctrl"));
    /* After the delay the unfinished part is discarded, not misread. */
    CHECK(events_are(&decoder, "\x1b[1;5", 5, 2000, ""));
    CHECK(events_are(&decoder, "C", 1, 2050, "key 'C'"));
    /* ESC [ and ESC O alone. */
    CHECK(events_are(&decoder, "\x1b[", 2, 3000, ""));
    CHECK(events_are(&decoder, "A", 1, 3100, "key '[' +alt, key 'A'"));
    CHECK(events_are(&decoder, "\x1bO", 2, 4000, ""));
    CHECK(events_are(&decoder, nullptr, 0, 4050, "key 'O' +alt"));
    /* A held partial scalar resolves too. */
    CHECK(events_are(&decoder, "\xe2\x82", 2, 5000, ""));
    CHECK(events_are(&decoder, "\xac", 1, 5010, "key U+20AC"));
    CHECK(events_are(&decoder, "\xe2\x82", 2, 6000, ""));
    CHECK(events_are(&decoder, "\xac", 1, 6050, "key U+FFFD, key U+FFFD"));
    /* A clock that runs backwards counts as no time passing. */
    CHECK(events_are(&decoder, "\x1b", 1, 7000, ""));
    CHECK(events_are(&decoder, nullptr, 0, 10, ""));
    CHECK(events_are(&decoder, nullptr, 0, 7050, "key Escape"));

    /* Delay 0: the ESC resolves at the next call, but bytes given with it
     * arrived together. */
    CHECK(ovd_decoder_init(&decoder, 0, text, sizeof text));
    CHECK(events_are(&decoder, "\x1b", 1, 0, ""));
    CHECK(events_are(&decoder, "[A", 2, 0, "key Escape, key '[', key 'A'"));
    CHECK(events_are(&decoder, "\x1b[A", 3, 0, "key Up"));

    /* The deadline saturates. */
    CHECK(ovd_decoder_init(&decoder, 1000, text, sizeof text));
    CHECK(events_are(&decoder, "\x1b", 1, UINT64_MAX - 10, ""));
    CHECK(ovd_decoder_deadline(&decoder, &deadline) && deadline == UINT64_MAX);
    CHECK(events_are(&decoder, nullptr, 0, UINT64_MAX, "key Escape"));
    return true;
}

/* ---- Arguments and the UTF-8 unit -------------------------------------- */

static bool test_arguments(void)
{
    OvdDecoder decoder;
    char text[OVD_DECODER_TEXT_MIN];
    memset(&decoder, 0x5A, sizeof decoder);
    CHECK(!ovd_decoder_init(nullptr, 50, text, sizeof text));
    CHECK(!ovd_decoder_init(&decoder, 50, nullptr, sizeof text));
    CHECK(!ovd_decoder_init(&decoder, 50, text, OVD_DECODER_TEXT_MIN - 1));
    CHECK(((const unsigned char *)&decoder)[0] == 0x5A); /* untouched */
    CHECK(ovd_decoder_init(&decoder, 50, text, sizeof text));
    CHECK(decoder.state == OVD_DECODER_GROUND);

    size_t consumed = 7;
    OvdEvent event;
    CHECK(!ovd_decoder_next(nullptr, "a", 1, 0, &consumed, &event));
    CHECK(!ovd_decoder_next(&decoder, nullptr, 1, 0, &consumed, &event));
    CHECK(!ovd_decoder_next(&decoder, "a", 1, 0, nullptr, &event));
    CHECK(!ovd_decoder_next(&decoder, "a", 1, 0, &consumed, nullptr));
    CHECK(ovd_decoder_next(&decoder, "a", 1, 0, &consumed, &event) && consumed == 1);
    CHECK(!ovd_decoder_next(&decoder, nullptr, 0, 0, &consumed, &event) && consumed == 0);
    CHECK(event.kind == OVD_EVENT_KIND_NONE);
    uint64_t deadline = 0;
    CHECK(!ovd_decoder_deadline(nullptr, &deadline));
    CHECK(!ovd_decoder_deadline(&decoder, nullptr));
    return true;
}

static bool unit_is(const char *bytes, size_t available, uint32_t code_point, uint8_t size,
                    OvdUtf8Status status)
{
    const OvdUtf8Unit unit = ovd_unicode_decode_utf8(bytes, available);
    return unit.code_point == code_point && unit.size == size && unit.status == status;
}

/* The decoder's view of unicode.c (design D1): a cut-short prefix is
 * TRUNCATED, an ill-formed one its maximal subpart. */
static bool test_utf8_unit(void)
{
    CHECK(unit_is(nullptr, 0, 0, 0, OVD_UTF8_TRUNCATED));
    CHECK(unit_is("a", 0, 0, 0, OVD_UTF8_TRUNCATED));
    CHECK(unit_is("a", 1, 'a', 1, OVD_UTF8_SCALAR));
    CHECK(unit_is("\0", 1, 0, 1, OVD_UTF8_SCALAR));
    CHECK(unit_is("\xc3\xa9", 2, 0xE9, 2, OVD_UTF8_SCALAR));
    CHECK(unit_is("\xc3", 1, 0, 1, OVD_UTF8_TRUNCATED));
    CHECK(unit_is("\xf0\x9f\x98", 3, 0, 3, OVD_UTF8_TRUNCATED));
    CHECK(unit_is("\xf0\x9f\x98\x80", 4, 0x1F600, 4, OVD_UTF8_SCALAR));
    CHECK(unit_is("\xf0\x9f" "A", 3, 0, 2, OVD_UTF8_ILL_FORMED));
    CHECK(unit_is("\xe0\x80", 2, 0, 1, OVD_UTF8_ILL_FORMED));
    CHECK(unit_is("\xed\xa0", 2, 0, 1, OVD_UTF8_ILL_FORMED));
    CHECK(unit_is("\xff", 1, 0, 1, OVD_UTF8_ILL_FORMED));
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "keys", test_keys },
        { "modifier_forms", test_modifier_forms },
        { "control_keys", test_control_keys },
        { "text_keys", test_text_keys },
        { "alt_keys", test_alt_keys },
        { "discarded", test_discarded },
        { "parameter_bound", test_parameter_bound },
        { "mouse", test_mouse },
        { "paste_and_focus", test_paste_and_focus },
        { "paste_held_back", test_paste_held_back },
        { "paste_chunks", test_paste_chunks },
        { "escape_delay", test_escape_delay },
        { "arguments", test_arguments },
        { "utf8_unit", test_utf8_unit },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (tests[i].run()) {
            printf("ok   %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    puts("decode: keys, modifiers, text, mouse, paste, focus, delay, and bounds passed");
    return EXIT_SUCCESS;
}
