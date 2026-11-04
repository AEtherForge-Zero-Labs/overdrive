/* The canvas (render.h): drawing into the open frame's grid through a
 * clip. Owning module of struct OvdCanvas (buffer.h). FR-030 to FR-033;
 * research.md R18 to R23. */
#include "buffer.h"
#include "unicode.h"

#include <overdrive/render.h>

#include <stdint.h>

/* The attribute bits render.h defines; others are dropped (R22). */
static constexpr uint16_t CANVAS_ATTR_MASK =
    (uint16_t)(OVD_ATTR_BOLD | OVD_ATTR_DIM | OVD_ATTR_ITALIC | OVD_ATTR_UNDERLINE
               | OVD_ATTR_REVERSE | OVD_ATTR_STRIKETHROUGH);

static constexpr OvdRect EMPTY_RECT = { 0, 0, 0, 0 };

/* A half-open span [lo, hi) in 64 bits, so int32_t inputs cannot
 * overflow (R18). */
typedef struct Span {
    int64_t lo;
    int64_t hi;
} Span;

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static bool is_open(const OvdCanvas *canvas)
{
    return canvas != nullptr && canvas->frame != nullptr;
}

/* The intersection of two rects, or EMPTY_RECT. A non-positive extent is
 * empty. When b lies within the canvas, so does the result, so its
 * fields fit in int32_t. */
static OvdRect intersect(OvdRect a, OvdRect b)
{
    const Span x = { max64(a.x, b.x), min64((int64_t)a.x + a.cols, (int64_t)b.x + b.cols) };
    const Span y = { max64(a.y, b.y), min64((int64_t)a.y + a.rows, (int64_t)b.y + b.rows) };
    if (a.cols <= 0 || a.rows <= 0 || b.cols <= 0 || b.rows <= 0 || x.hi <= x.lo
        || y.hi <= y.lo) {
        return EMPTY_RECT;
    }
    return (OvdRect){ (int32_t)x.lo, (int32_t)y.lo, (int32_t)(x.hi - x.lo),
                      (int32_t)(y.hi - y.lo) };
}

static bool clip_contains(OvdRect clip, int64_t x, int64_t y)
{
    return x >= clip.x && x < (int64_t)clip.x + clip.cols && y >= clip.y
           && y < (int64_t)clip.y + clip.rows;
}

/* Only the fields of the color's kind survive; an unknown kind is
 * DEFAULT (R22), so colors that look the same compare equal. */
static OvdColor normalize_color(OvdColor color)
{
    switch (color.kind) {
    case OVD_COLOR_KIND_INDEXED:
        return ovd_color_indexed(color.index);
    case OVD_COLOR_KIND_RGB:
        return ovd_color_rgb(color.r, color.g, color.b);
    case OVD_COLOR_KIND_DEFAULT:
        break;
    }
    return OVD_COLOR_DEFAULT;
}

static OvdStyle normalize_style(OvdStyle style)
{
    return (OvdStyle){
        .fg = normalize_color(style.fg),
        .bg = normalize_color(style.bg),
        .attrs = (uint16_t)(style.attrs & CANVAS_ATTR_MASK),
    };
}

void ovd_canvas_open(OvdCanvas *canvas, OvdBuffer *frame)
{
    ovd_buffer_clear(frame);
    const OvdSize size = ovd_buffer_size(frame);
    canvas->frame = frame;
    canvas->clip = (size.cols > 0 && size.rows > 0) ? (OvdRect){ 0, 0, size.cols, size.rows }
                                                     : EMPTY_RECT;
}

void ovd_canvas_close(OvdCanvas *canvas)
{
    canvas->frame = nullptr;
    canvas->clip = EMPTY_RECT;
}

OvdSize ovd_canvas_size(const OvdCanvas *canvas)
{
    return is_open(canvas) ? ovd_buffer_size(canvas->frame) : (OvdSize){ 0, 0 };
}

OvdRect ovd_canvas_clip(const OvdCanvas *canvas)
{
    return is_open(canvas) ? canvas->clip : EMPTY_RECT;
}

OvdRect ovd_canvas_set_clip(OvdCanvas *canvas, OvdRect clip)
{
    if (!is_open(canvas)) {
        return EMPTY_RECT;
    }
    const OvdSize size = ovd_buffer_size(canvas->frame);
    const OvdRect previous = canvas->clip;
    canvas->clip = intersect(clip, (OvdRect){ 0, 0, size.cols, size.rows });
    return previous;
}

void ovd_canvas_fill(OvdCanvas *canvas, OvdRect area, OvdStyle style)
{
    if (!is_open(canvas)) {
        return;
    }
    const OvdRect r = intersect(area, canvas->clip);
    const OvdStyle clean = normalize_style(style);
    for (int32_t y = r.y; y < r.y + r.rows; ++y) {
        for (int32_t x = r.x; x < r.x + r.cols; ++x) {
            ovd_buffer_put_space(canvas->frame, x, y, clean);
        }
    }
}

int32_t ovd_canvas_text(OvdCanvas *canvas, int32_t x, int32_t y, const char *text,
                        size_t length, OvdStyle style)
{
    if (!is_open(canvas) || text == nullptr) {
        return 0;
    }
    const OvdStyle clean = normalize_style(style);
    const OvdRect clip = canvas->clip;
    /* Past this column nothing can be drawn; the column stops there so
     * it never overflows, while the total keeps counting. */
    const int64_t stop = (int64_t)INT32_MAX + 2;
    int64_t column = x;
    int64_t total = 0;
    size_t offset = 0;
    OvdGrapheme grapheme;
    while (ovd_unicode_next_grapheme(text, length, &offset, &grapheme)) {
        const int64_t width = grapheme.width;
        if (column < stop) {
            const bool first = clip_contains(clip, column, y);
            const bool second = width == 2 && clip_contains(clip, column + 1, y);
            if (width == 2 && first && second) {
                ovd_buffer_put(canvas->frame, (int32_t)column, y, &grapheme, clean);
            } else if (width == 2) {
                /* A cut wide grapheme: its inside half is a space (R19). */
                if (first) {
                    ovd_buffer_put_space(canvas->frame, (int32_t)column, y, clean);
                }
                if (second) {
                    ovd_buffer_put_space(canvas->frame, (int32_t)(column + 1), y, clean);
                }
            } else if (first) {
                ovd_buffer_put(canvas->frame, (int32_t)column, y, &grapheme, clean);
            }
            column += width;
        }
        total = min64(total + width, INT32_MAX);
    }
    return (int32_t)total;
}

bool ovd_canvas_cell(const OvdCanvas *canvas, int32_t x, int32_t y, OvdCellView *out)
{
    if (!is_open(canvas) || out == nullptr) {
        return false;
    }
    const OvdCell *cell = ovd_buffer_cell(canvas->frame, x, y);
    if (cell == nullptr) {
        return false;
    }
    *out = (OvdCellView){
        .text = cell->text,
        .length = cell->length,
        .width = cell->width,
        .style = cell->style,
    };
    return true;
}
