/* Unicode decoding, grapheme segmentation, and width: research R15–R17.
 * Transformational (Art. IV): nothing is retained between calls. */
#include "unicode.h"

#include "unicode_tables.h" /* generated into the build tree (Art. II) */

#include <string.h>

/* How one decoded unit takes part in a grapheme. */
typedef enum UnitKind : uint8_t {
    UNIT_BASE,     /* starts a grapheme */
    UNIT_EXTENDER, /* Mn, Me, or Cf other than a bidi control (R15) */
    UNIT_REPLACED, /* drawn as U+FFFD (R17); a base */
} UnitKind;

/* One decoded code point, or one maximal subpart of ill-formed UTF-8. */
typedef struct Unit {
    uint32_t code_point; /* meaningless when kind is UNIT_REPLACED */
    uint8_t size;        /* input bytes covered, 1–4 */
    UnitKind kind;
} Unit;

static const uint32_t zero_width_joiner = 0x200D;
static const char replacement_character[] = "\xEF\xBF\xBD"; /* U+FFFD */
static const uint8_t replacement_size = 3;

static bool in_ranges(const uint32_t (*ranges)[2], size_t count, uint32_t code_point)
{
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (code_point < ranges[middle][0]) {
            high = middle;
        } else if (code_point > ranges[middle][1]) {
            low = middle + 1;
        } else {
            return true;
        }
    }
    return false;
}

/* The bidi controls of R17, which would let caller text reorder the
 * terminal's display. All are Cf, so this is checked before extenders. */
static bool is_bidi_control(uint32_t code_point)
{
    return code_point == 0x061C || code_point == 0x200E || code_point == 0x200F
        || (code_point >= 0x202A && code_point <= 0x202E)
        || (code_point >= 0x2066 && code_point <= 0x2069);
}

static Unit classify(uint32_t code_point, uint8_t size)
{
    UnitKind kind = UNIT_BASE;
    if (code_point < 0x20 || (code_point >= 0x7F && code_point <= 0x9F)
            || code_point == 0x2028 || code_point == 0x2029
            || is_bidi_control(code_point)) {
        kind = UNIT_REPLACED;
    } else if (in_ranges(ovd_unicode_extender_ranges,
                         sizeof(ovd_unicode_extender_ranges)
                             / sizeof(ovd_unicode_extender_ranges[0]),
                         code_point)) {
        kind = UNIT_EXTENDER;
    }
    return (Unit){ .code_point = code_point, .size = size, .kind = kind };
}

static Unit ill_formed(uint8_t size)
{
    return (Unit){ .code_point = 0, .size = size, .kind = UNIT_REPLACED };
}

/* Decodes the unit at bytes[0..available), available >= 1, per the
 * Unicode Standard, Ch. 3, Table 3-7 (well-formed UTF-8 byte sequences).
 * An ill-formed sequence yields its maximal subpart: the longest prefix
 * of a well-formed sequence, or one byte if none ("U+FFFD Substitution of
 * Maximal Subparts"). The byte that ends a subpart is not consumed. */
static Unit decode(const unsigned char *bytes, size_t available)
{
    const unsigned char lead = bytes[0];
    if (lead < 0x80) {
        return classify(lead, 1);
    }
    uint8_t trails;
    uint32_t code_point;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        trails = 1;
        code_point = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        trails = 2;
        code_point = lead & 0x0Fu;
        if (lead == 0xE0) {
            low = 0xA0; /* no overlongs */
        } else if (lead == 0xED) {
            high = 0x9F; /* no surrogates */
        }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        trails = 3;
        code_point = lead & 0x07u;
        if (lead == 0xF0) {
            low = 0x90; /* no overlongs */
        } else if (lead == 0xF4) {
            high = 0x8F; /* nothing above U+10FFFF */
        }
    } else {
        return ill_formed(1); /* 80..C1, F5..FF never start a sequence */
    }
    uint8_t size = 1;
    for (uint8_t i = 0; i < trails; ++i) {
        if (size >= available) {
            return ill_formed(size); /* truncated by length */
        }
        const unsigned char trail = bytes[size];
        if (trail < low || trail > high) {
            return ill_formed(size);
        }
        code_point = (code_point << 6) | (trail & 0x3Fu);
        low = 0x80;
        high = 0xBF;
        ++size;
    }
    return classify(code_point, size);
}

static uint8_t base_width(uint32_t code_point)
{
    const bool wide = in_ranges(ovd_unicode_wide_ranges,
                                sizeof(ovd_unicode_wide_ranges)
                                    / sizeof(ovd_unicode_wide_ranges[0]),
                                code_point);
    return wide ? 2 : 1;
}

/* Appends size bytes to the cell text unless that would pass the cell's
 * limit; then marks it overflowed and stops copying for good. */
static void append(OvdGrapheme *grapheme, bool *overflowed, const void *bytes, size_t size)
{
    if (*overflowed || size > (size_t)OVD_GRAPHEME_MAX_BYTES - grapheme->length) {
        *overflowed = true;
        return;
    }
    memcpy(grapheme->bytes + grapheme->length, bytes, size);
    grapheme->length = (uint8_t)(grapheme->length + size); /* <= 15 by the check above */
}

bool ovd_unicode_next_grapheme(const char *text, size_t length,
                               size_t *offset, OvdGrapheme *out)
{
    if (text == nullptr || offset == nullptr || out == nullptr || *offset >= length) {
        return false;
    }
    /* UTF-8 is defined on octets; read char storage as unsigned bytes. */
    const unsigned char *bytes = (const unsigned char *)text;
    size_t position = *offset;

    OvdGrapheme grapheme = { .length = 0, .width = 1, .replaced = false };
    bool overflowed = false;

    /* The base. An extender here has no base before it: lone (R15). */
    const Unit base = decode(bytes + position, length - position);
    if (base.kind == UNIT_BASE) {
        append(&grapheme, &overflowed, bytes + position, base.size);
        grapheme.width = base_width(base.code_point);
    } else {
        append(&grapheme, &overflowed, replacement_character, replacement_size);
        grapheme.replaced = true;
    }
    position += base.size;

    /* Extenders, and after a ZWJ the next unit unless R17 replaces it. A
     * replaced ZWJ was the base above, so it joins nothing. */
    bool after_joiner = false;
    while (position < length) {
        const Unit next = decode(bytes + position, length - position);
        if (next.kind == UNIT_REPLACED
                || (next.kind != UNIT_EXTENDER && !after_joiner)) {
            break;
        }
        append(&grapheme, &overflowed, bytes + position, next.size);
        after_joiner = next.code_point == zero_width_joiner;
        position += next.size;
    }

    if (overflowed) {
        /* Longer than a cell holds: the whole grapheme is one U+FFFD (R17). */
        grapheme = (OvdGrapheme){ .length = 0, .width = 1, .replaced = true };
        memcpy(grapheme.bytes, replacement_character, replacement_size);
        grapheme.length = replacement_size;
    }

    *offset = position;
    *out = grapheme;
    return true;
}
