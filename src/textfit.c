/* Text fitting for the widgets: textfit.h, 004 R308 and R318. */
#include "textfit.h"

#include "unicode.h"

#include <overdrive/render.h>

static const char ellipsis[] = "\xE2\x80\xA6"; /* U+2026, 1 column (R318) */

OvdFit ovd_textfit_measure(const char *text, size_t length, int32_t width, OvdAlign align)
{
    const int32_t w = width > 0 ? width : 0;
    if (w == 0) {
        return (OvdFit){ 0 };
    }
    if (text == nullptr) {
        length = 0;
    }

    /* Walk graphemes while the text could still fit. keep_* is the
     * longest prefix within w - 1 columns, the part a cut keeps. */
    int64_t total = 0;
    size_t offset = 0;
    size_t keep_length = 0;
    int64_t keep_width = 0;
    size_t first_length = 0;
    uint8_t first_width = 0;
    bool fits = true;
    OvdGrapheme grapheme;
    while (ovd_unicode_next_grapheme(text, length, &offset, &grapheme)) {
        if (first_length == 0) {
            first_length = offset;
            first_width = grapheme.width;
        }
        total += grapheme.width;
        if (total > w) {
            fits = false;
            break;
        }
        if (total <= (int64_t)w - 1) {
            keep_length = offset;
            keep_width = total;
        }
    }

    if (fits) {
        const int32_t pad = (int32_t)(w - total);
        const bool right = align == OVD_ALIGN_RIGHT;
        return (OvdFit){
            .length = offset,
            .width = (int32_t)total,
            .mark = false,
            .pad_left = right ? pad : 0,
            .pad_right = right ? 0 : pad,
        };
    }
    if (w == 1) {
        if (first_width == 1) {
            return (OvdFit){ .length = first_length, .width = 1 };
        }
        return (OvdFit){ .pad_right = 1 };
    }
    return (OvdFit){
        .length = keep_length,
        .width = (int32_t)keep_width,
        .mark = true,
        .pad_right = (int32_t)((int64_t)w - 1 - keep_width),
    };
}

/* Fills count cells of row y from column x with spaces. x starts at an
 * int32_t and only grows, so it is never below INT32_MIN; columns past
 * INT32_MAX are not drawn. */
static void spaces(OvdCanvas *canvas, int64_t x, int32_t y, int64_t count, OvdStyle style)
{
    if (count <= 0 || x > INT32_MAX) {
        return;
    }
    const int64_t room = (int64_t)INT32_MAX - x + 1;
    /* count is a pad of a width <= INT32_MAX, so cols fits an int32_t. */
    const int64_t cols = count < room ? count : room;
    ovd_canvas_fill(canvas, (OvdRect){ (int32_t)x, y, (int32_t)cols, 1 }, style);
}

void ovd_textfit_draw(OvdCanvas *canvas, int32_t x, int32_t y, int32_t width, const char *text,
                      size_t length, OvdAlign align, OvdStyle style)
{
    if (canvas == nullptr || width <= 0) {
        return;
    }
    const OvdFit fit = ovd_textfit_measure(text, length, width, align);
    int64_t column = x;
    spaces(canvas, column, y, fit.pad_left, style);
    column += fit.pad_left;
    if (fit.length > 0 && column <= INT32_MAX) {
        ovd_canvas_text(canvas, (int32_t)column, y, text, fit.length, style);
    }
    column += fit.width;
    if (fit.mark && column <= INT32_MAX) {
        ovd_canvas_text(canvas, (int32_t)column, y, ellipsis, sizeof ellipsis - 1, style);
    }
    column += fit.mark ? 1 : 0;
    spaces(canvas, column, y, fit.pad_right, style);
}
