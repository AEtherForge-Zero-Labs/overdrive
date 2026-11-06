/* Text fitting for the widgets (specs/004-widgets, R308; data-model.md,
 * "Internal: text fitting"). Internal to Overdrive. Transformational
 * (Art. IV): nothing is retained between calls.
 *
 * It measures with ovd_unicode_next_grapheme, the iterator the canvas
 * draws with, so the width it measures is the width ovd_canvas_text
 * spans for the same bytes. It cuts only at grapheme boundaries, and the
 * canvas segments a prefix cut there into the same graphemes. */
#ifndef OVERDRIVE_SRC_TEXTFIT_H
#define OVERDRIVE_SRC_TEXTFIT_H

#include <overdrive/widgets.h>

#include <stddef.h>
#include <stdint.h>

/* How text fits into a width w. The cells, from the left: pad_left
 * spaces, then text[0, length) taking width columns, then U+2026 if mark,
 * then pad_right spaces. They always add up to w (0 when w <= 0).
 *
 * - Text whose width is at most w fits. It is drawn whole and aligned:
 *   OVD_ALIGN_LEFT pads on the right, OVD_ALIGN_RIGHT on the left.
 * - Otherwise, with w >= 2: the graphemes from the start whose total
 *   width is at most w - 1, then the mark, then padding on the right. A
 *   wide grapheme that would cross w - 1 is left out, so the pad is 1. Cut
 *   text always fills from the left, whatever the alignment.
 * - With w == 1 and text that does not fit: the first grapheme if it is 1
 *   wide, otherwise a space. No mark.
 * - With w <= 0: nothing.
 *
 * nullptr text is empty whatever length says; an out-of-range align reads
 * as OVD_ALIGN_LEFT. Measuring stops once the text is known not to fit, so
 * it reads about w graphemes, not the whole text. */
typedef struct OvdFit {
    size_t length;     /* bytes of text drawn, from its start */
    int32_t width;     /* columns those bytes take */
    bool mark;         /* U+2026 follows them */
    int32_t pad_left;  /* spaces before the text */
    int32_t pad_right; /* spaces after the text and the mark */
} OvdFit;

[[nodiscard]] OvdFit ovd_textfit_measure(const char *text, size_t length, int32_t width,
                                         OvdAlign align);

/* Fills exactly width cells of row y from column x with text fitted as
 * ovd_textfit_measure says, all in style: the padding as spaces, the text
 * through ovd_canvas_text, and the mark. Cells off the canvas or outside
 * its clip are discarded by the canvas; coordinates past INT32_MAX are not
 * drawn. A nullptr canvas, or width <= 0, draws nothing. */
void ovd_textfit_draw(OvdCanvas *canvas, int32_t x, int32_t y, int32_t width, const char *text,
                      size_t length, OvdAlign align, OvdStyle style);

#endif /* OVERDRIVE_SRC_TEXTFIT_H */
