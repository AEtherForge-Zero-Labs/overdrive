/* Unicode decoding, grapheme segmentation, and width (FR-031, FR-032).
 * Internal to Overdrive. Rules: research R15 (graphemes), R16 (width),
 * R17 (replacement); data: the pinned UCD in third_party/ucd/, version in
 * cmake/gen_unicode_tables.cmake. */
#ifndef OVERDRIVE_SRC_UNICODE_H
#define OVERDRIVE_SRC_UNICODE_H

#include <stddef.h>
#include <stdint.h>

/* The most UTF-8 bytes one cell's grapheme holds (spec Assumptions). */
enum : uint8_t { OVD_GRAPHEME_MAX_BYTES = 15 };

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
