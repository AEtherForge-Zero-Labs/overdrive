/* Unicode decoding, grapheme segmentation, and width (FR-031, FR-032).
 * Internal to Overdrive. Rules: research R15 (graphemes), R16 (width),
 * R17 (replacement); data: the pinned UCD in third_party/ucd/, version in
 * cmake/gen_unicode_tables.cmake. */
#ifndef OVERDRIVE_SRC_UNICODE_H
#define OVERDRIVE_SRC_UNICODE_H

#include <stddef.h>
#include <stdint.h>

/* The most UTF-8 bytes one cell's grapheme holds (spec Assumptions). */
constexpr uint8_t OVD_GRAPHEME_MAX_BYTES = 15;

/* One grapheme as a cell stores it.
 *
 * Invariants, established by ovd_unicode_next_grapheme:
 * - 1 <= length <= OVD_GRAPHEME_MAX_BYTES; bytes[0..length) is
 *   well-formed UTF-8 and contains no C0 control, DEL, C1 control,
 *   U+2028, U+2029, or R17 bidi control, so no byte of it can reach a
 *   terminal as a control sequence;
 * - width is 2 if the base's East_Asian_Width is W or F, else 1;
 * - replaced is true when the base is a U+FFFD substituted for something
 *   (R17), and then width is 1 and bytes begin with EF BF BD. Extenders
 *   that followed the replaced input are kept after it. */
typedef struct OvdGrapheme {
    char bytes[OVD_GRAPHEME_MAX_BYTES];
    uint8_t length;
    uint8_t width;
    bool replaced;
} OvdGrapheme;

typedef enum OvdUtf8Status : uint8_t {
    /* A well-formed scalar value of size bytes. */
    OVD_UTF8_SCALAR = 0,
    /* A maximal subpart of ill-formed UTF-8, size bytes (1 to 3): the
     * byte after it ended it and is not part of it. */
    OVD_UTF8_ILL_FORMED,
    /* All size bytes given are a proper prefix of a well-formed sequence:
     * more bytes decide it. */
    OVD_UTF8_TRUNCATED,
} OvdUtf8Status;

/* One UTF-8 unit. code_point is the scalar value for OVD_UTF8_SCALAR,
 * else 0. */
typedef struct OvdUtf8Unit {
    uint32_t code_point;
    uint8_t size;
    OvdUtf8Status status;
} OvdUtf8Unit;

/* Decodes the unit at bytes[0..available) per the Unicode Standard,
 * Ch. 3, Table 3-7, reading no byte at or past available. The grapheme
 * iterator below uses the same decoder, treating TRUNCATED as one more
 * ill-formed subpart. Returns { 0, 0, OVD_UTF8_TRUNCATED } when bytes is
 * nullptr or available is 0. */
[[nodiscard]] OvdUtf8Unit ovd_unicode_decode_utf8(const char *bytes, size_t available);

/* Writes the UTF-8 form of code_point to out and returns its byte count,
 * 1 to 4 (Unicode Standard, Ch. 3, Table 3-6). A surrogate (U+D800 to
 * U+DFFF) or a value above U+10FFFF is not a scalar value: nothing is
 * written, and it returns 0. 004 R314. */
[[nodiscard]] uint8_t ovd_unicode_encode_utf8(uint32_t code_point, char out[4]);

/* Reads the grapheme that starts at text[*offset], stores it in *out, and
 * advances *offset past every input byte it covers. Whatever is at
 * *offset starts a grapheme: an extender there is lone and becomes U+FFFD.
 * NUL is a C0 control, not a terminator; only length ends the text.
 *
 * Returns false, leaving *offset and *out untouched, when text, offset,
 * or out is nullptr or *offset >= length. Each call that returns true
 * advances *offset by at least one byte, so a loop terminates. */
[[nodiscard]] bool ovd_unicode_next_grapheme(const char *text, size_t length,
                                             size_t *offset, OvdGrapheme *out);

#endif /* OVERDRIVE_SRC_UNICODE_H */
