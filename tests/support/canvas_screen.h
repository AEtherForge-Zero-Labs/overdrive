/*
 * Test-only canvas read-back for widget tests (specs/004-widgets, R319).
 *
 * Widget tests draw on a headless terminal's canvas, then read every cell
 * back with ovd_canvas_cell into the screen model (screen.h), and compare
 * it with an expected screen written as text rows plus style-key rows
 * (Art. VI). Public API only.
 */
#ifndef OVERDRIVE_TEST_CANVAS_SCREEN_H
#define OVERDRIVE_TEST_CANVAS_SCREEN_H

#include "screen.h"

#include <overdrive/overdrive.h>

#include <stddef.h>
#include <stdint.h>

/* A headless terminal with an open frame, and its canvas. */
typedef struct CanvasScreen {
    OvdTerminal *terminal;
    OvdCanvas *canvas;
} CanvasScreen;

/* Creates a headless terminal of cols x rows with a discarding sink and
 * begins a frame. Returns false, leaving *fixture zeroed, on failure. */
[[nodiscard]] bool canvas_screen_open(int32_t cols, int32_t rows, CanvasScreen *fixture);

/* Abandons the frame and destroys the terminal. Safe on a zeroed fixture. */
void canvas_screen_close(CanvasScreen *fixture);

/* Reads every cell of canvas into a new screen of the canvas's size: a
 * lead cell through test_screen_put, a width-0 continuation skipped.
 * Returns nullptr if the canvas is closed or the screen cannot be made. */
[[nodiscard]] TestScreen *canvas_screen_capture(const OvdCanvas *canvas);

/* Compares canvas with the screen that text_rows and style_rows describe
 * (test_screen_from_rows, one entry per canvas row; style_rows may be
 * nullptr). On a mismatch, prints both dumps and the first difference to
 * stderr and returns false. */
[[nodiscard]] bool canvas_screen_expect(const OvdCanvas *canvas, const char *const text_rows[],
                                        const char *const style_rows[],
                                        const TestStyleKey legend[], size_t legend_count);

/* The classifier for the code points the widget tests use, read from
 * third_party/ucd, not from src/unicode (001 R38: the oracle must not
 * share the module it checks):
 * - U+4E00..U+9FFF: East_Asian_Width W, so wide;
 * - U+0300..U+036F: General_Category Mn, so an extender;
 * - everything else narrow, which covers ASCII, U+2026 and U+FFFD (both
 *   East_Asian_Width A, drawn 1 wide by Overdrive, 001 R16). */
TestWidth canvas_screen_classify(uint32_t code_point);

#endif
