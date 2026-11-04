/*
 * Box drawing (FR-034; research.md R18, R44). Transformational over the
 * canvas: every border cell is drawn through ovd_canvas_text, so the
 * clip, the R20 repair, R22 normalization, and closed canvases behave
 * exactly as for text. Nothing here reads the canvas's fields.
 */
#include <overdrive/render.h>

#include <stdint.h>
#include <string.h>

/* One border style. Every glyph is East Asian Width A or Na: one column. */
typedef struct BoxGlyphs {
    const char *top_left;
    const char *top_right;
    const char *bottom_left;
    const char *bottom_right;
    const char *horizontal;
    const char *vertical;
} BoxGlyphs;

/* Indexed by OvdBorder. */
static const BoxGlyphs GLYPHS[] = {
    [OVD_BORDER_LIGHT] = { "┌", "┐", "└", "┘", "─", "│" },
    [OVD_BORDER_HEAVY] = { "┏", "┓", "┗", "┛", "━", "┃" },
    [OVD_BORDER_DOUBLE] = { "╔", "╗", "╚", "╝", "═", "║" },
    [OVD_BORDER_ROUNDED] = { "╭", "╮", "╰", "╯", "─", "│" },
    [OVD_BORDER_ASCII] = { "+", "+", "+", "+", "-", "|" },
};

/* The parts of one box in 64-bit coordinates (R18), and where it is drawn. */
typedef struct Box {
    OvdCanvas *canvas;
    OvdSize size;
    OvdStyle style;
    int64_t x0, x1, y0, y1;
} Box;

/* Draws glyph at (x, y) if the cell is on the canvas; the clip is the
 * canvas's to apply. On the canvas, both coordinates fit in int32_t. */
static void put(const Box *box, int64_t x, int64_t y, const char *glyph)
{
    if (x < 0 || y < 0 || x >= box->size.cols || y >= box->size.rows) {
        return;
    }
    ovd_canvas_text(box->canvas, (int32_t)x, (int32_t)y, glyph, strlen(glyph), box->style);
}

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

/* Row y: the two corners, then the edge between them, limited to the
 * canvas, so the loop runs at most cols times. */
static void row(const Box *box, int64_t y, const char *left, const char *right,
                const char *edge)
{
    if (y < 0 || y >= box->size.rows) {
        return;
    }
    put(box, box->x0, y, left);
    put(box, box->x1, y, right);
    const int64_t last = min64(box->x1 - 1, (int64_t)box->size.cols - 1);
    for (int64_t x = max64(box->x0 + 1, 0); x <= last; ++x) {
        put(box, x, y, edge);
    }
}

/* Column x between the corner rows, limited to the canvas. */
static void column(const Box *box, int64_t x, const char *edge)
{
    if (x < 0 || x >= box->size.cols) {
        return;
    }
    const int64_t last = min64(box->y1 - 1, (int64_t)box->size.rows - 1);
    for (int64_t y = max64(box->y0 + 1, 0); y <= last; ++y) {
        put(box, x, y, edge);
    }
}

void ovd_canvas_box(OvdCanvas *canvas, OvdRect area, OvdBorder border, OvdStyle style)
{
    if (area.cols < 2 || area.rows < 2 || border > OVD_BORDER_ASCII) {
        return;
    }
    /* A closed or nullptr canvas has size {0, 0} (R23): nothing is drawn. */
    const Box box = {
        .canvas = canvas,
        .size = ovd_canvas_size(canvas),
        .style = style,
        .x0 = area.x,
        .x1 = (int64_t)area.x + area.cols - 1,
        .y0 = area.y,
        .y1 = (int64_t)area.y + area.rows - 1,
    };
    const BoxGlyphs *glyphs = &GLYPHS[border];
    row(&box, box.y0, glyphs->top_left, glyphs->top_right, glyphs->horizontal);
    row(&box, box.y1, glyphs->bottom_left, glyphs->bottom_right, glyphs->horizontal);
    column(&box, box.x0, glyphs->vertical);
    column(&box, box.x1, glyphs->vertical);
}
