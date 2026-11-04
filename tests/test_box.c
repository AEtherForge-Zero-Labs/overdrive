/* Tests for src/box.c (T029): FR-034, User Story 3; research.md R18 to
 * R20, R22, R23, R45. The canvas is opened directly on a buffer, as in
 * test_canvas.c. Every cell starts as '.' in DOT, so a cell the box must
 * not touch reads back as '.'. */
#include "alloc.h"
#include "buffer.h"
#include "support/test_alloc.h"

#include <overdrive/render.h>

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

typedef struct Fixture {
    TestAlloc alloc;
    OvdAllocator allocator;
    OvdBuffer buffer;
    OvdCanvas canvas;
} Fixture;

static const OvdStyle DOT = {
    .fg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 8 },
    .bg = { .kind = OVD_COLOR_KIND_DEFAULT },
    .attrs = OVD_ATTR_DIM,
};

/* The style boxes are drawn in, as stored after R22 normalization. */
static const OvdStyle LINE = {
    .fg = { .kind = OVD_COLOR_KIND_RGB, .r = 10, .g = 20, .b = 30 },
    .bg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 4 },
    .attrs = OVD_ATTR_BOLD,
};

static bool color_eq(OvdColor a, OvdColor b)
{
    return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

static bool style_eq(OvdStyle a, OvdStyle b)
{
    return color_eq(a.fg, b.fg) && color_eq(a.bg, b.bg) && a.attrs == b.attrs;
}

/* Every cell of the canvas becomes '.' in DOT. */
static void dots(OvdCanvas *canvas)
{
    const OvdSize size = ovd_canvas_size(canvas);
    const OvdRect clip = ovd_canvas_set_clip(canvas, (OvdRect){ 0, 0, size.cols, size.rows });
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            ovd_canvas_text(canvas, x, y, ".", 1, DOT);
        }
    }
    ovd_canvas_set_clip(canvas, clip);
}

static bool fixture_open(Fixture *f, int32_t cols, int32_t rows)
{
    memset(f, 0, sizeof *f);
    test_alloc_init(&f->alloc);
    f->allocator = test_alloc_allocator(&f->alloc);
    const OvdSize size = { cols, rows };
    OvdBlockRequest request;
    OvdBlock block = { nullptr, 0 };
    if (ovd_buffer_request(size, &request) != OVD_OK
        || ovd_alloc_all(&f->allocator, &request, 1, &block) != OVD_OK) {
        return false;
    }
    ovd_buffer_adopt(&f->buffer, &f->allocator, &block, size);
    ovd_canvas_open(&f->canvas, &f->buffer);
    dots(&f->canvas);
    return true;
}

static bool fixture_close(Fixture *f)
{
    ovd_canvas_close(&f->canvas);
    ovd_buffer_release(&f->buffer, &f->allocator);
    return f->alloc.live_blocks == 0 && f->alloc.violations == 0;
}

/* Byte length of the UTF-8 sequence that starts with lead. */
static size_t utf8_length(unsigned char lead)
{
    return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
}

/* Row y reads as text, one narrow cell per code point. A '.' cell must be
 * in DOT; every other cell in LINE. */
static bool row_is(const OvdCanvas *canvas, int32_t y, const char *text)
{
    int32_t x = 0;
    for (size_t i = 0; text[i] != '\0'; ++x) {
        const size_t n = utf8_length((unsigned char)text[i]);
        OvdCellView view;
        if (!ovd_canvas_cell(canvas, x, y, &view)) {
            fprintf(stderr, "  cell (%d, %d) unreadable\n", (int)x, (int)y);
            return false;
        }
        const OvdStyle expected = text[i] == '.' ? DOT : LINE;
        if (view.length != n || memcmp(view.text, text + i, n) != 0 || view.width != 1
            || !style_eq(view.style, expected)) {
            fprintf(stderr, "  row %d, column %d: got \"%s\" (width %u), expected \"%.*s\"\n",
                    (int)y, (int)x, view.text, (unsigned)view.width, (int)n, text + i);
            return false;
        }
        i += n;
    }
    if (x != ovd_canvas_size(canvas).cols) {
        fprintf(stderr, "  row %d: expected text has %d columns\n", (int)y, (int)x);
        return false;
    }
    return true;
}

static bool rows_are(const OvdCanvas *canvas, const char *const rows[], int32_t count)
{
    CHECK(ovd_canvas_size(canvas).rows == count);
    for (int32_t y = 0; y < count; ++y) {
        CHECK(row_is(canvas, y, rows[y]));
    }
    return true;
}

static bool all_dots(const OvdCanvas *canvas)
{
    const OvdSize size = ovd_canvas_size(canvas);
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            OvdCellView view;
            CHECK(ovd_canvas_cell(canvas, x, y, &view));
            CHECK(view.length == 1 && view.text[0] == '.' && style_eq(view.style, DOT));
        }
    }
    return true;
}

/* ---- Tests ------------------------------------------------------------ */

/* US3 scenario 1, in all five styles: a 10x4 box; the interior and the
 * cells around it are left as they were. */
static bool test_five_styles(void)
{
    static const struct {
        OvdBorder border;
        const char *rows[6];
    } cases[] = {
        { OVD_BORDER_LIGHT,
          { "............", ".┌────────┐.", ".│........│.", ".│........│.", ".└────────┘.",
            "............" } },
        { OVD_BORDER_HEAVY,
          { "............", ".┏━━━━━━━━┓.", ".┃........┃.", ".┃........┃.", ".┗━━━━━━━━┛.",
            "............" } },
        { OVD_BORDER_DOUBLE,
          { "............", ".╔════════╗.", ".║........║.", ".║........║.", ".╚════════╝.",
            "............" } },
        { OVD_BORDER_ROUNDED,
          { "............", ".╭────────╮.", ".│........│.", ".│........│.", ".╰────────╯.",
            "............" } },
        { OVD_BORDER_ASCII,
          { "............", ".+--------+.", ".|........|.", ".|........|.", ".+--------+.",
            "............" } },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        Fixture f;
        CHECK(fixture_open(&f, 12, 6));
        ovd_canvas_box(&f.canvas, (OvdRect){ 1, 1, 10, 4 }, cases[i].border, LINE);
        if (!rows_are(&f.canvas, cases[i].rows, 6)) {
            fprintf(stderr, "  border %d\n", (int)cases[i].border);
            return false;
        }
        CHECK(fixture_close(&f));
    }
    return true;
}

/* R22: the box's style is normalized like any drawn style. */
static bool test_style_normalized(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 3));
    const OvdStyle raw = {
        .fg = { .kind = OVD_COLOR_KIND_RGB, .index = 99, .r = 10, .g = 20, .b = 30 },
        .bg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 4, .r = 1, .g = 2, .b = 3 },
        .attrs = (uint16_t)(OVD_ATTR_BOLD | 0x8000u),
    };
    ovd_canvas_box(&f.canvas, (OvdRect){ 0, 0, 4, 3 }, OVD_BORDER_LIGHT, raw);
    static const char *const rows[] = { "┌──┐", "│..│", "└──┘" };
    CHECK(rows_are(&f.canvas, rows, 3));
    CHECK(fixture_close(&f));
    return true;
}

/* 2-wide and 2-tall boxes: corners, and edges only along the long side. */
static bool test_narrow_boxes(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 9, 4));
    ovd_canvas_box(&f.canvas, (OvdRect){ 0, 0, 2, 2 }, OVD_BORDER_LIGHT, LINE);
    ovd_canvas_box(&f.canvas, (OvdRect){ 3, 0, 2, 4 }, OVD_BORDER_HEAVY, LINE);
    ovd_canvas_box(&f.canvas, (OvdRect){ 5, 2, 4, 2 }, OVD_BORDER_DOUBLE, LINE);
    static const char *const rows[] = {
        "┌┐.┏┓....",
        "└┘.┃┃....",
        "...┃┃╔══╗",
        "...┗┛╚══╝",
    };
    CHECK(rows_are(&f.canvas, rows, 4));
    CHECK(fixture_close(&f));
    return true;
}

/* Narrower or shorter than 2 cells, and out-of-range borders, draw
 * nothing. */
static bool test_draws_nothing(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 6, 5));
    static const OvdRect areas[] = {
        { 0, 0, 1, 5 },
        { 0, 0, 5, 1 },
        { 1, 1, 0, 0 },
        { 1, 1, -3, 4 },
        { 1, 1, 4, -3 },
        { 1, 1, 1, 1 },
        { 0, 0, INT32_MIN, INT32_MIN },
        { 0, 0, 1, INT32_MAX },
        { 0, 0, INT32_MAX, 1 },
    };
    for (size_t i = 0; i < sizeof areas / sizeof areas[0]; ++i) {
        ovd_canvas_box(&f.canvas, areas[i], OVD_BORDER_LIGHT, LINE);
    }
    ovd_canvas_box(&f.canvas, (OvdRect){ 0, 0, 4, 4 }, (OvdBorder)5, LINE);
    ovd_canvas_box(&f.canvas, (OvdRect){ 0, 0, 4, 4 }, (OvdBorder)255, LINE);
    CHECK(all_dots(&f.canvas));
    CHECK(fixture_close(&f));
    return true;
}

/* US3 scenario 2: only cells inside the clip change. */
static bool test_clipped(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 12, 6));
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 0, 0, 4, 3 });
    ovd_canvas_box(&f.canvas, (OvdRect){ 1, 1, 10, 4 }, OVD_BORDER_LIGHT, LINE);
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 9, 3, 3, 3 });
    ovd_canvas_box(&f.canvas, (OvdRect){ 1, 1, 10, 4 }, OVD_BORDER_ROUNDED, LINE);
    /* A clip inside the interior: nothing to draw. */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 3, 2, 5, 2 });
    ovd_canvas_box(&f.canvas, (OvdRect){ 1, 1, 10, 4 }, OVD_BORDER_HEAVY, LINE);
    /* Rows 1-2: the light box's top-left, cut by the first clip. Rows 3-4:
     * the rounded box's bottom-right, cut by the second. */
    static const char *const rows[] = {
        "............",
        ".┌──........",
        ".│..........",
        "..........│.",
        ".........─╯.",
        "............",
    };
    CHECK(rows_are(&f.canvas, rows, 6));
    CHECK(fixture_close(&f));
    return true;
}

/* Areas partly or wholly off the canvas, at any int32_t extent (R18). */
static bool test_off_canvas(void)
{
    {
        Fixture f;
        CHECK(fixture_open(&f, 12, 6));
        ovd_canvas_box(&f.canvas, (OvdRect){ -3, -2, 6, 5 }, OVD_BORDER_LIGHT, LINE);
        static const char *const rows[] = {
            "..│.........", "..│.........", "──┘.........",
            "............", "............", "............",
        };
        CHECK(rows_are(&f.canvas, rows, 6));
        CHECK(fixture_close(&f));
    }
    {
        Fixture f;
        CHECK(fixture_open(&f, 12, 6));
        ovd_canvas_box(&f.canvas, (OvdRect){ 1, 1, INT32_MAX, INT32_MAX }, OVD_BORDER_LIGHT,
                       LINE);
        static const char *const rows[] = {
            "............", ".┌──────────", ".│..........",
            ".│..........", ".│..........", ".│..........",
        };
        CHECK(rows_are(&f.canvas, rows, 6));
        CHECK(fixture_close(&f));
    }
    {
        Fixture f;
        CHECK(fixture_open(&f, 12, 6));
        /* Right edge at x = INT32_MIN + INT32_MAX - 1 = -2: off the canvas. */
        ovd_canvas_box(&f.canvas, (OvdRect){ INT32_MIN, 0, INT32_MAX, 6 }, OVD_BORDER_LIGHT,
                       LINE);
        ovd_canvas_box(&f.canvas, (OvdRect){ -5, -5, INT32_MAX, INT32_MAX }, OVD_BORDER_LIGHT,
                       LINE);
        ovd_canvas_box(&f.canvas, (OvdRect){ INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX },
                       OVD_BORDER_LIGHT, LINE);
        ovd_canvas_box(&f.canvas, (OvdRect){ INT32_MIN, INT32_MIN, 2, 2 }, OVD_BORDER_LIGHT,
                       LINE);
        ovd_canvas_box(&f.canvas, (OvdRect){ 12, 0, 4, 4 }, OVD_BORDER_LIGHT, LINE);
        ovd_canvas_box(&f.canvas, (OvdRect){ 0, 6, 4, 4 }, OVD_BORDER_LIGHT, LINE);
        CHECK(all_dots(&f.canvas));
        CHECK(fixture_close(&f));
    }
    {
        /* The box ends exactly on the canvas's last row and column. */
        Fixture f;
        CHECK(fixture_open(&f, 3, 2));
        ovd_canvas_box(&f.canvas, (OvdRect){ 0, 0, 3, 2 }, OVD_BORDER_ASCII, LINE);
        static const char *const rows[] = { "+-+", "+-+" };
        CHECK(rows_are(&f.canvas, rows, 2));
        CHECK(fixture_close(&f));
    }
    return true;
}

/* R20: an edge that overwrites half of a wide glyph lying across the
 * clip turns its other half into a space in its own style. */
static bool test_wide_repair(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 8, 3));
    const OvdStyle wide_style = { .fg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 2 } };
    ovd_canvas_text(&f.canvas, 4, 0, "\xE7\x95\x8C", 3, wide_style); /* U+754C, wide */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 0, 0, 5, 3 });
    ovd_canvas_box(&f.canvas, (OvdRect){ 0, 0, 5, 3 }, OVD_BORDER_LIGHT, LINE);
    OvdCellView view;
    CHECK(ovd_canvas_cell(&f.canvas, 4, 0, &view));
    CHECK(strcmp(view.text, "┐") == 0 && view.width == 1 && style_eq(view.style, LINE));
    CHECK(ovd_canvas_cell(&f.canvas, 5, 0, &view));
    CHECK(strcmp(view.text, " ") == 0 && view.width == 1 && style_eq(view.style, wide_style));
    CHECK(fixture_close(&f));
    return true;
}

/* R23: a closed or nullptr canvas draws nothing. Drawing never allocates. */
static bool test_closed_and_no_allocation(void)
{
    ovd_canvas_box(nullptr, (OvdRect){ 0, 0, 4, 4 }, OVD_BORDER_LIGHT, LINE);

    Fixture f;
    CHECK(fixture_open(&f, 6, 4));
    const size_t calls = f.alloc.allocate_calls;
    ovd_canvas_box(&f.canvas, (OvdRect){ 0, 0, 6, 4 }, OVD_BORDER_DOUBLE, LINE);
    ovd_canvas_box(&f.canvas, (OvdRect){ -9, -9, INT32_MAX, INT32_MAX }, OVD_BORDER_HEAVY,
                   LINE);
    CHECK(f.alloc.allocate_calls == calls);

    ovd_canvas_close(&f.canvas);
    dots(&f.canvas); /* closed: draws nothing either */
    ovd_canvas_box(&f.canvas, (OvdRect){ 1, 1, 4, 2 }, OVD_BORDER_LIGHT, LINE);
    /* The open-canvas double box is there; the closed-canvas box is not. */
    const OvdCell *cell = ovd_buffer_cell(&f.buffer, 0, 1);
    CHECK(cell != nullptr && strcmp(cell->text, "║") == 0);
    cell = ovd_buffer_cell(&f.buffer, 1, 0);
    CHECK(cell != nullptr && strcmp(cell->text, "═") == 0);
    cell = ovd_buffer_cell(&f.buffer, 1, 1);
    CHECK(cell != nullptr && strcmp(cell->text, ".") == 0);
    CHECK(f.alloc.allocate_calls == calls);
    CHECK(fixture_close(&f));
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "five_styles", test_five_styles },
        { "style_normalized", test_style_normalized },
        { "narrow_boxes", test_narrow_boxes },
        { "draws_nothing", test_draws_nothing },
        { "clipped", test_clipped },
        { "off_canvas", test_off_canvas },
        { "wide_repair", test_wide_repair },
        { "closed_and_no_allocation", test_closed_and_no_allocation },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures == 0) {
        puts("box: five styles, normalization, narrow boxes, nothing drawn, clip, "
             "off-canvas, wide repair, closed canvas passed");
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
