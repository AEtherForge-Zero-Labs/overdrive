/*
 * Test-only screen model: the readable form of terminal state (Art. VI).
 *
 * A screen is a grid of cells. Each cell holds one grapheme (UTF-8, at most
 * 15 bytes), its width, and its OvdStyle. A wide grapheme occupies two
 * cells: the lead has width 2 and the grapheme's text; the continuation has
 * width 0, empty text, and the lead's style. Invariant: a continuation cell
 * always follows a lead of width 2, and a lead of width 2 is always followed
 * by its continuation. When a write splits a wide grapheme, the half that
 * was not written becomes a space marked indeterminate, because terminals
 * disagree about what a split glyph shows; an indeterminate cell never
 * compares equal to anything until it is overwritten.
 *
 * Expected screens are written as text rows plus optional style-key rows
 * (one ASCII key per column; '.' is OVD_STYLE_DEFAULT, every other key is
 * looked up in a legend), so a failing comparison reads as text.
 *
 * Glyph widths come from the caller's classifier, not from src/unicode:
 * the oracle must not share the module it checks, and this keeps the
 * screen independent of the Unicode tables (research R37).
 */
#ifndef OVERDRIVE_TEST_SCREEN_H
#define OVERDRIVE_TEST_SCREEN_H

#include <overdrive/render.h>

#include <stddef.h>
#include <stdint.h>

/* How a code point takes part in a grapheme (R15, R16). */
typedef enum TestWidth : uint8_t {
    TEST_WIDTH_EXTEND = 0, /* attaches to the grapheme before it */
    TEST_WIDTH_NARROW = 1, /* a base one column wide */
    TEST_WIDTH_WIDE = 2,   /* a base two columns wide */
} TestWidth;

/* Classifies one Unicode scalar value. U+200D (ZWJ) is always treated as
 * an extender that also joins the next code point, whatever this returns. */
typedef TestWidth (*TestClassify)(uint32_t code_point);

enum : uint32_t { TEST_ZWJ = 0x200D };

enum : size_t { TEST_GRAPHEME_MAX = 15 };

typedef struct TestCell {
    char text[TEST_GRAPHEME_MAX + 1]; /* NUL-terminated */
    uint8_t length;                       /* bytes in text */
    uint8_t width;                        /* 1 or 2 for a lead, 0 for a continuation */
    bool indeterminate;                   /* half of a split wide glyph */
    OvdStyle style;
} TestCell;

/* A style key for expected screens: key must not be '.'. */
typedef struct TestStyleKey {
    char key;
    OvdStyle style;
} TestStyleKey;

typedef struct TestScreen TestScreen;

/* A blank screen: every cell a space, width 1, OVD_STYLE_DEFAULT. Returns
 * nullptr for a non-positive size, a size whose cell count overflows, or
 * an allocation failure. */
[[nodiscard]] TestScreen *test_screen_create(int32_t cols, int32_t rows);

void test_screen_destroy(TestScreen *screen);

int32_t test_screen_cols(const TestScreen *screen);
int32_t test_screen_rows(const TestScreen *screen);

/* The cell at (x, y), or nullptr outside the screen. */
const TestCell *test_screen_cell(const TestScreen *screen, int32_t x, int32_t y);

/* Sets every cell to a determinate space in OVD_STYLE_DEFAULT. */
void test_screen_clear(TestScreen *screen);

/* Marks every cell indeterminate (contents unknown to the model). */
void test_screen_forget(TestScreen *screen);

/* Writes one grapheme as a lead at (x, y), plus its continuation at x + 1
 * when width is 2. The caller guarantees 1 <= length <= 15, width 1 or 2,
 * and that the grapheme fits on the row. Any wide grapheme this splits
 * leaves its other half indeterminate. */
void test_screen_put(TestScreen *screen, int32_t x, int32_t y,
                         const char *text, size_t length, uint8_t width, OvdStyle style);

/* Appends extender bytes to the lead cell at (x, y). Returns false, changing
 * nothing, if (x, y) is not a lead or the text would exceed 15 bytes. */
[[nodiscard]] bool test_screen_extend(TestScreen *screen, int32_t x, int32_t y,
                                          const char *bytes, size_t length);

/* Decodes one well-formed UTF-8 scalar value at the start of bytes.
 * Returns its length (1-4) and stores it in *out, or 0 for an ill-formed
 * or truncated sequence (surrogates, overlongs, and values above U+10FFFF
 * are ill-formed). */
[[nodiscard]] size_t test_utf8_decode(const unsigned char *bytes, size_t length,
                                          uint32_t *out);

/* True for code points the canvas never holds (R17): C0, DEL, C1,
 * U+2028, U+2029, and the bidi controls. */
[[nodiscard]] bool test_code_point_forbidden(uint32_t code_point);

/* Builds an expected screen from rows[0..rows) of UTF-8 text and, if styles
 * is not nullptr, styles[0..rows) of style keys. A text row may be shorter
 * than the screen (the rest is default spaces) and so may a style row (the
 * rest is '.'). Both columns of a wide glyph need the same key. On error,
 * returns nullptr and writes a message naming the row and column into
 * error (always NUL-terminated when capacity > 0). */
[[nodiscard]] TestScreen *test_screen_from_rows(
    int32_t cols, int32_t rows, TestClassify classify,
    const char *const text[], const char *const styles[],
    const TestStyleKey legend[], size_t legend_count,
    char *error, size_t capacity);

/* True if both screens have the same size and every cell is equal and
 * determinate. Otherwise writes the first difference, naming the cell and
 * both values, into message (always NUL-terminated when capacity > 0). */
[[nodiscard]] bool test_screen_equal(const TestScreen *expected,
                                         const TestScreen *actual,
                                         char *message, size_t capacity);

/* Writes the screen's text, one line per row ('?' for an indeterminate
 * cell, nothing for a continuation), into out, truncating to capacity and
 * always NUL-terminating when capacity > 0. Returns the bytes written,
 * excluding the NUL. */
size_t test_screen_dump(const TestScreen *screen, char *out, size_t capacity);

#endif
