/* Tests for src/buffer.c and src/canvas.c (T024): FR-030 to FR-033,
 * research.md R18 to R23. The canvas is opened directly on a buffer;
 * frames belong to the terminal (OVD-10). */
#include "alloc.h"
#include "buffer.h"
#include "support/test_alloc.h"
#include "unicode.h"

#include <overdrive/render.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FFFD "\xEF\xBF\xBD"
#define WIDE "\xE7\x95\x8C"  /* U+754C, East Asian Width W */
#define ACUTE "\xCC\x81"     /* U+0301 COMBINING ACUTE ACCENT, Mn */

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

/* A canvas open on its own buffer, with storage from a counting allocator. */
typedef struct Fixture {
    TestAlloc alloc;
    OvdAllocator allocator;
    OvdBuffer buffer;
    OvdCanvas canvas;
} Fixture;

static bool fixture_open(Fixture *f, int32_t cols, int32_t rows)
{
    memset(f, 0, sizeof *f);
    test_alloc_init(&f->alloc);
    f->allocator = test_alloc_allocator(&f->alloc);
    const OvdSize size = { cols, rows };
    OvdBlockRequest request;
    if (ovd_buffer_request(size, &request) != OVD_OK) {
        return false;
    }
    OvdBlock block = { nullptr, 0 };
    if (ovd_alloc_all(&f->allocator, &request, 1, &block) != OVD_OK) {
        return false;
    }
    ovd_buffer_adopt(&f->buffer, &f->allocator, &block, size);
    ovd_canvas_open(&f->canvas, &f->buffer);
    return true;
}

static bool fixture_close(Fixture *f)
{
    ovd_canvas_close(&f->canvas);
    ovd_buffer_release(&f->buffer, &f->allocator);
    return f->alloc.live_blocks == 0 && f->alloc.violations == 0;
}

static const OvdStyle RED = {
    .fg = { .kind = OVD_COLOR_KIND_RGB, .r = 255 },
    .bg = { .kind = OVD_COLOR_KIND_DEFAULT },
    .attrs = OVD_ATTR_BOLD,
};
static const OvdStyle BLUE = {
    .fg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 4 },
    .bg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 7 },
    .attrs = OVD_ATTR_UNDERLINE,
};

static bool color_eq(OvdColor a, OvdColor b)
{
    return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

static bool style_eq(OvdStyle a, OvdStyle b)
{
    return color_eq(a.fg, b.fg) && color_eq(a.bg, b.bg) && a.attrs == b.attrs;
}

static bool rect_eq(OvdRect a, OvdRect b)
{
    return a.x == b.x && a.y == b.y && a.cols == b.cols && a.rows == b.rows;
}

/* The cell at (x, y) holds text, has width, and has style. */
static bool cell_is(const OvdCanvas *canvas, int32_t x, int32_t y, const char *text,
                    uint8_t width, OvdStyle style)
{
    OvdCellView view;
    if (!ovd_canvas_cell(canvas, x, y, &view)) {
        fprintf(stderr, "  cell (%d, %d) unreadable\n", (int)x, (int)y);
        return false;
    }
    const size_t length = strlen(text);
    const bool ok = view.length == length && memcmp(view.text, text, length) == 0
                    && view.text[length] == '\0' && view.width == width
                    && style_eq(view.style, style);
    if (!ok) {
        fprintf(stderr, "  cell (%d, %d): \"%s\" length %zu width %u\n", (int)x, (int)y,
                view.text, view.length, (unsigned)view.width);
    }
    return ok;
}

static bool is_blank(const OvdCanvas *canvas, int32_t x, int32_t y)
{
    return cell_is(canvas, x, y, " ", 1, OVD_STYLE_DEFAULT);
}

/* Every cell of the grid keeps the pairing invariant. */
static bool paired(const OvdBuffer *buffer)
{
    const OvdSize size = ovd_buffer_size(buffer);
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            const OvdCell *cell = ovd_buffer_cell(buffer, x, y);
            if (cell->width == 2) {
                const OvdCell *next = ovd_buffer_cell(buffer, x + 1, y);
                if (next == nullptr || next->width != 0) {
                    return false;
                }
            } else if (cell->width == 0) {
                const OvdCell *prev = ovd_buffer_cell(buffer, x - 1, y);
                if (prev == nullptr || prev->width != 2 || cell->length != 0) {
                    return false;
                }
            } else if (cell->width != 1) {
                return false;
            }
        }
    }
    return true;
}

static int32_t text(OvdCanvas *canvas, int32_t x, int32_t y, const char *s, OvdStyle style)
{
    return ovd_canvas_text(canvas, x, y, s, strlen(s), style);
}

/* ---- Storage --------------------------------------------------------- */

static bool test_request(void)
{
    OvdBlockRequest request = { 7, 7 };
    CHECK(ovd_buffer_request((OvdSize){ 0, 5 }, &request) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_buffer_request((OvdSize){ 5, -1 }, &request) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_buffer_request((OvdSize){ INT32_MIN, INT32_MIN }, &request)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_buffer_request((OvdSize){ 3, 2 }, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(request.count == 7 && request.element_size == 7);

    CHECK(ovd_buffer_request((OvdSize){ 1, 1 }, &request) == OVD_OK);
    CHECK(request.count == 1 && request.element_size == sizeof(OvdCell));
    CHECK(ovd_buffer_request((OvdSize){ 80, 24 }, &request) == OVD_OK);
    CHECK(request.count == 80u * 24u && request.element_size == sizeof(OvdCell));

    /* cols x rows overflows size_t only where size_t is narrower than 62 bits. */
    const OvdResult huge = ovd_buffer_request((OvdSize){ INT32_MAX, INT32_MAX }, &request);
    if ((uint64_t)SIZE_MAX / (uint64_t)INT32_MAX < (uint64_t)INT32_MAX) {
        CHECK(huge == OVD_ERROR_ALLOCATION);
    } else {
        CHECK(huge == OVD_OK);
        CHECK(request.count == (size_t)INT32_MAX * (size_t)INT32_MAX);
    }
    return true;
}

static bool test_adopt_and_release(void)
{
    TestAlloc alloc;
    test_alloc_init(&alloc);
    const OvdAllocator allocator = test_alloc_allocator(&alloc);

    OvdBuffer buffer = {};
    CHECK(ovd_buffer_size(&buffer).cols == 0 && ovd_buffer_size(&buffer).rows == 0);
    CHECK(ovd_buffer_cell(&buffer, 0, 0) == nullptr);
    ovd_buffer_release(&buffer, &allocator); /* empty: no allocator call */
    CHECK(alloc.release_calls == 0);

    OvdBlockRequest request;
    CHECK(ovd_buffer_request((OvdSize){ 3, 2 }, &request) == OVD_OK);
    OvdBlock block = { nullptr, 0 };
    CHECK(ovd_alloc_all(&allocator, &request, 1, &block) == OVD_OK);
    ovd_buffer_adopt(&buffer, &allocator, &block, (OvdSize){ 3, 2 });
    CHECK(block.data == nullptr && block.size == 0);
    CHECK(ovd_buffer_size(&buffer).cols == 3 && ovd_buffer_size(&buffer).rows == 2);
    for (int32_t y = 0; y < 2; ++y) {
        for (int32_t x = 0; x < 3; ++x) {
            const OvdCell *cell = ovd_buffer_cell(&buffer, x, y);
            CHECK(cell != nullptr && cell->length == 1 && strcmp(cell->text, " ") == 0);
            CHECK(cell->width == 1 && style_eq(cell->style, OVD_STYLE_DEFAULT));
        }
    }
    CHECK(ovd_buffer_cell(&buffer, 3, 0) == nullptr);
    CHECK(ovd_buffer_cell(&buffer, 0, 2) == nullptr);
    CHECK(ovd_buffer_cell(&buffer, -1, 0) == nullptr);

    /* A refused resize leaves the old grid intact (R31). */
    CHECK(ovd_buffer_put_space(&buffer, 1, 1, RED));
    alloc.refuse_all = true;
    CHECK(ovd_buffer_request((OvdSize){ 4, 4 }, &request) == OVD_OK);
    CHECK(ovd_alloc_all(&allocator, &request, 1, &block) == OVD_ERROR_ALLOCATION);
    CHECK(ovd_buffer_size(&buffer).cols == 3);
    CHECK(style_eq(ovd_buffer_cell(&buffer, 1, 1)->style, RED));
    alloc.refuse_all = false;

    /* A resize allocates the new block before releasing the old one. */
    const size_t allocated = alloc.allocate_calls;
    CHECK(ovd_alloc_all(&allocator, &request, 1, &block) == OVD_OK);
    CHECK(alloc.live_blocks == 2);
    ovd_buffer_adopt(&buffer, &allocator, &block, (OvdSize){ 4, 4 });
    CHECK(alloc.allocate_calls == allocated + 1 && alloc.live_blocks == 1);
    CHECK(ovd_buffer_size(&buffer).cols == 4 && ovd_buffer_size(&buffer).rows == 4);
    CHECK(style_eq(ovd_buffer_cell(&buffer, 1, 1)->style, OVD_STYLE_DEFAULT));

    ovd_buffer_release(&buffer, &allocator);
    CHECK(ovd_buffer_size(&buffer).cols == 0 && ovd_buffer_cell(&buffer, 0, 0) == nullptr);
    CHECK(alloc.live_blocks == 0 && alloc.violations == 0);
    return true;
}

/* ---- Pairing in the grid (R20) --------------------------------------- */

static bool grapheme_of(const char *s, OvdGrapheme *out)
{
    size_t offset = 0;
    return ovd_unicode_next_grapheme(s, strlen(s), &offset, out);
}

static bool test_buffer_pairing(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 1));
    OvdGrapheme wide;
    OvdGrapheme narrow;
    CHECK(grapheme_of(WIDE, &wide) && wide.width == 2);
    CHECK(grapheme_of("a", &narrow) && narrow.width == 1);

    /* Refused writes change nothing. */
    CHECK(!ovd_buffer_put(&f.buffer, 3, 0, &wide, RED));
    CHECK(!ovd_buffer_put(&f.buffer, 4, 0, &narrow, RED));
    CHECK(!ovd_buffer_put(&f.buffer, -1, 0, &narrow, RED));
    CHECK(!ovd_buffer_put(&f.buffer, 0, 1, &narrow, RED));
    CHECK(!ovd_buffer_put_space(&f.buffer, 4, 0, RED));
    CHECK(!ovd_buffer_put_space(&f.buffer, 0, -1, RED));
    for (int32_t x = 0; x < 4; ++x) {
        CHECK(is_blank(&f.canvas, x, 0));
    }

    /* Overlapping wide writes: the displaced half becomes a space. */
    CHECK(ovd_buffer_put(&f.buffer, 0, 0, &wide, RED));
    CHECK(ovd_buffer_put(&f.buffer, 1, 0, &wide, BLUE));
    CHECK(cell_is(&f.canvas, 0, 0, " ", 1, RED));
    CHECK(cell_is(&f.canvas, 1, 0, WIDE, 2, BLUE));
    CHECK(cell_is(&f.canvas, 2, 0, "", 0, BLUE));
    CHECK(paired(&f.buffer));

    /* Overwriting the first half spaces the second, in its own style. */
    CHECK(ovd_buffer_put(&f.buffer, 1, 0, &narrow, RED));
    CHECK(cell_is(&f.canvas, 1, 0, "a", 1, RED));
    CHECK(cell_is(&f.canvas, 2, 0, " ", 1, BLUE));
    CHECK(paired(&f.buffer));

    /* Overwriting the second half spaces the first. */
    CHECK(ovd_buffer_put(&f.buffer, 2, 0, &wide, BLUE));
    CHECK(ovd_buffer_put_space(&f.buffer, 3, 0, RED));
    CHECK(cell_is(&f.canvas, 2, 0, " ", 1, BLUE));
    CHECK(cell_is(&f.canvas, 3, 0, " ", 1, RED));
    CHECK(paired(&f.buffer));

    /* Rewriting a wide glyph in place keeps both halves. */
    CHECK(ovd_buffer_put(&f.buffer, 0, 0, &wide, RED));
    CHECK(ovd_buffer_put(&f.buffer, 0, 0, &wide, BLUE));
    CHECK(cell_is(&f.canvas, 0, 0, WIDE, 2, BLUE));
    CHECK(cell_is(&f.canvas, 1, 0, "", 0, BLUE));
    CHECK(paired(&f.buffer));
    CHECK(fixture_close(&f));
    return true;
}

/* ---- Closed and nullptr canvases (R22, R23) -------------------------- */

static bool closed_behaves(OvdCanvas *canvas)
{
    CHECK(ovd_canvas_size(canvas).cols == 0 && ovd_canvas_size(canvas).rows == 0);
    CHECK(rect_eq(ovd_canvas_clip(canvas), (OvdRect){ 0, 0, 0, 0 }));
    CHECK(rect_eq(ovd_canvas_set_clip(canvas, (OvdRect){ 0, 0, 2, 2 }), (OvdRect){ 0, 0, 0, 0 }));
    CHECK(rect_eq(ovd_canvas_clip(canvas), (OvdRect){ 0, 0, 0, 0 }));
    CHECK(text(canvas, 0, 0, "abc", RED) == 0);
    ovd_canvas_fill(canvas, (OvdRect){ 0, 0, 10, 10 }, RED);
    OvdCellView view = { .text = "sentinel", .length = 8 };
    CHECK(!ovd_canvas_cell(canvas, 0, 0, &view));
    CHECK(view.length == 8 && strcmp(view.text, "sentinel") == 0);
    return true;
}

static bool test_closed(void)
{
    CHECK(closed_behaves(nullptr));
    OvdCanvas zero = {};
    CHECK(closed_behaves(&zero));

    Fixture f;
    CHECK(fixture_open(&f, 4, 2));
    CHECK(text(&f.canvas, 0, 0, "ab", BLUE) == 2);
    CHECK(ovd_canvas_set_clip(&f.canvas, (OvdRect){ 1, 1, 1, 1 }).cols == 4);
    ovd_canvas_close(&f.canvas);
    CHECK(closed_behaves(&f.canvas));
    /* Nothing reached the grid while closed; it stays as drawn. */
    CHECK(strcmp(ovd_buffer_cell(&f.buffer, 0, 0)->text, "a") == 0);
    CHECK(strcmp(ovd_buffer_cell(&f.buffer, 2, 0)->text, " ") == 0);
    CHECK(style_eq(ovd_buffer_cell(&f.buffer, 0, 1)->style, OVD_STYLE_DEFAULT));

    /* Reopening clears and restores the full clip. */
    ovd_canvas_open(&f.canvas, &f.buffer);
    CHECK(rect_eq(ovd_canvas_clip(&f.canvas), (OvdRect){ 0, 0, 4, 2 }));
    CHECK(is_blank(&f.canvas, 0, 0) && is_blank(&f.canvas, 1, 0));
    CHECK(fixture_close(&f));
    return true;
}

/* ---- Open canvas: size, clip, fill (R18) ----------------------------- */

static bool test_open_and_clip(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 5, 3));
    CHECK(ovd_canvas_size(&f.canvas).cols == 5 && ovd_canvas_size(&f.canvas).rows == 3);
    CHECK(rect_eq(ovd_canvas_clip(&f.canvas), (OvdRect){ 0, 0, 5, 3 }));
    for (int32_t y = 0; y < 3; ++y) {
        for (int32_t x = 0; x < 5; ++x) {
            CHECK(is_blank(&f.canvas, x, y));
        }
    }

    /* set_clip intersects with the bounds and returns the previous clip. */
    CHECK(rect_eq(ovd_canvas_set_clip(&f.canvas, (OvdRect){ -2, 1, 4, 10 }),
                  (OvdRect){ 0, 0, 5, 3 }));
    CHECK(rect_eq(ovd_canvas_clip(&f.canvas), (OvdRect){ 0, 1, 2, 2 }));
    const OvdRect saved = ovd_canvas_set_clip(&f.canvas, (OvdRect){ 3, 0, 1, 1 });
    CHECK(rect_eq(saved, (OvdRect){ 0, 1, 2, 2 }));
    CHECK(rect_eq(ovd_canvas_set_clip(&f.canvas, saved), (OvdRect){ 3, 0, 1, 1 }));
    CHECK(rect_eq(ovd_canvas_clip(&f.canvas), saved));

    /* Empty intersections are stored as {0, 0, 0, 0}. */
    const OvdRect empties[] = {
        { 0, 0, 0, 3 },
        { 0, 0, 5, -1 },
        { 5, 0, 1, 1 },
        { -1, 0, 1, 1 },
        { 0, 3, 5, 5 },
        { INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX },
        { INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX },
        { INT32_MIN, 0, INT32_MIN, 3 },
        { 2, 1, INT32_MIN, INT32_MIN },
    };
    for (size_t i = 0; i < sizeof empties / sizeof empties[0]; ++i) {
        ovd_canvas_set_clip(&f.canvas, empties[i]);
        CHECK(rect_eq(ovd_canvas_clip(&f.canvas), (OvdRect){ 0, 0, 0, 0 }));
        ovd_canvas_fill(&f.canvas, (OvdRect){ 0, 0, 5, 3 }, RED);
        CHECK(text(&f.canvas, 0, 0, "abcde", RED) == 5);
    }
    for (int32_t y = 0; y < 3; ++y) {
        for (int32_t x = 0; x < 5; ++x) {
            CHECK(is_blank(&f.canvas, x, y));
        }
    }

    /* Extreme rectangles that still cover the canvas. */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX });
    CHECK(rect_eq(ovd_canvas_clip(&f.canvas), (OvdRect){ 0, 0, 0, 0 }));
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ -3, -3, INT32_MAX, INT32_MAX });
    CHECK(rect_eq(ovd_canvas_clip(&f.canvas), (OvdRect){ 0, 0, 5, 3 }));

    /* fill: only the clip's part of the area. */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 1, 0, 3, 2 });
    ovd_canvas_fill(&f.canvas, (OvdRect){ -5, 1, INT32_MAX, INT32_MAX }, RED);
    for (int32_t y = 0; y < 3; ++y) {
        for (int32_t x = 0; x < 5; ++x) {
            if (y == 1 && x >= 1 && x <= 3) {
                CHECK(cell_is(&f.canvas, x, y, " ", 1, RED));
            } else {
                CHECK(is_blank(&f.canvas, x, y));
            }
        }
    }
    ovd_canvas_fill(&f.canvas, (OvdRect){ 2, 0, 0, 3 }, BLUE);
    ovd_canvas_fill(&f.canvas, (OvdRect){ 2, 0, -4, 3 }, BLUE);
    ovd_canvas_fill(&f.canvas, (OvdRect){ INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX }, BLUE);
    ovd_canvas_fill(&f.canvas, (OvdRect){ INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN }, BLUE);
    CHECK(is_blank(&f.canvas, 2, 0) && cell_is(&f.canvas, 2, 1, " ", 1, RED));

    /* Reads outside the canvas fail and leave *out untouched. */
    OvdCellView view = { .text = "x", .length = 1, .width = 9 };
    CHECK(!ovd_canvas_cell(&f.canvas, 5, 0, &view));
    CHECK(!ovd_canvas_cell(&f.canvas, 0, 3, &view));
    CHECK(!ovd_canvas_cell(&f.canvas, -1, 0, &view));
    CHECK(!ovd_canvas_cell(&f.canvas, INT32_MIN, INT32_MAX, &view));
    CHECK(view.width == 9 && strcmp(view.text, "x") == 0);
    CHECK(!ovd_canvas_cell(&f.canvas, 0, 0, nullptr));
    CHECK(fixture_close(&f));
    return true;
}

/* ---- Text ------------------------------------------------------------ */

static bool test_text_basics(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 6, 2));
    CHECK(text(&f.canvas, 1, 0, "ab", RED) == 2);
    CHECK(is_blank(&f.canvas, 0, 0));
    CHECK(cell_is(&f.canvas, 1, 0, "a", 1, RED) && cell_is(&f.canvas, 2, 0, "b", 1, RED));
    CHECK(is_blank(&f.canvas, 3, 0));

    /* A wide grapheme spans two cells; a combining mark joins its base. */
    CHECK(text(&f.canvas, 0, 1, WIDE "e" ACUTE "z", BLUE) == 4);
    CHECK(cell_is(&f.canvas, 0, 1, WIDE, 2, BLUE));
    CHECK(cell_is(&f.canvas, 1, 1, "", 0, BLUE));
    CHECK(cell_is(&f.canvas, 2, 1, "e" ACUTE, 1, BLUE));
    CHECK(cell_is(&f.canvas, 3, 1, "z", 1, BLUE));

    /* Empty and nullptr text draw nothing. */
    CHECK(ovd_canvas_text(&f.canvas, 0, 0, "", 0, RED) == 0);
    CHECK(ovd_canvas_text(&f.canvas, 0, 0, nullptr, 5, RED) == 0);
    CHECK(ovd_canvas_text(&f.canvas, 0, 0, "abc", 0, RED) == 0);
    CHECK(is_blank(&f.canvas, 0, 0));

    /* Clipped text still returns its full width. */
    CHECK(text(&f.canvas, 4, 0, "wxyz", RED) == 4);
    CHECK(cell_is(&f.canvas, 4, 0, "w", 1, RED) && cell_is(&f.canvas, 5, 0, "x", 1, RED));
    CHECK(text(&f.canvas, -2, 0, "pqr", RED) == 3);
    CHECK(cell_is(&f.canvas, 0, 0, "r", 1, RED));
    CHECK(text(&f.canvas, 0, 2, "hidden", RED) == 6);
    CHECK(text(&f.canvas, 0, -1, WIDE WIDE, RED) == 4);
    CHECK(text(&f.canvas, INT32_MAX, 0, "ab", RED) == 2);
    CHECK(text(&f.canvas, INT32_MIN, 1, WIDE "ab", RED) == 4);
    CHECK(text(&f.canvas, INT32_MAX - 1, INT32_MIN, WIDE WIDE, RED) == 4);
    CHECK(paired(&f.buffer));

    /* Clip limits text. */
    ovd_canvas_fill(&f.canvas, (OvdRect){ 0, 0, 6, 2 }, OVD_STYLE_DEFAULT);
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 1, 0, 2, 1 });
    CHECK(text(&f.canvas, 0, 0, "abcd", RED) == 4);
    CHECK(is_blank(&f.canvas, 0, 0));
    CHECK(cell_is(&f.canvas, 1, 0, "b", 1, RED) && cell_is(&f.canvas, 2, 0, "c", 1, RED));
    CHECK(is_blank(&f.canvas, 3, 0));
    CHECK(text(&f.canvas, 1, 1, "a", RED) == 1);
    CHECK(is_blank(&f.canvas, 1, 1));
    CHECK(fixture_close(&f));
    return true;
}

/* R19: a wide grapheme with a half outside the clip or the canvas. */
static bool test_cut_wide(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 1));
    /* Right edge of the canvas. */
    CHECK(text(&f.canvas, 3, 0, WIDE, RED) == 2);
    CHECK(cell_is(&f.canvas, 3, 0, " ", 1, RED));
    /* Left edge of the canvas. */
    CHECK(text(&f.canvas, -1, 0, WIDE, BLUE) == 2);
    CHECK(cell_is(&f.canvas, 0, 0, " ", 1, BLUE));
    CHECK(is_blank(&f.canvas, 1, 0));
    CHECK(paired(&f.buffer));

    /* Right edge of the clip: the outside half is untouched. */
    ovd_canvas_fill(&f.canvas, (OvdRect){ 0, 0, 4, 1 }, OVD_STYLE_DEFAULT);
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 0, 0, 2, 1 });
    CHECK(text(&f.canvas, 1, 0, WIDE, RED) == 2);
    CHECK(is_blank(&f.canvas, 0, 0));
    CHECK(cell_is(&f.canvas, 1, 0, " ", 1, RED));
    CHECK(is_blank(&f.canvas, 2, 0));

    /* Left edge of the clip: first half outside. */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 2, 0, 2, 1 });
    CHECK(text(&f.canvas, 1, 0, WIDE "a", BLUE) == 3);
    CHECK(cell_is(&f.canvas, 1, 0, " ", 1, RED));
    CHECK(cell_is(&f.canvas, 2, 0, " ", 1, BLUE));
    CHECK(cell_is(&f.canvas, 3, 0, "a", 1, BLUE));
    CHECK(paired(&f.buffer));
    CHECK(fixture_close(&f));

    /* A 1x1 canvas cannot hold a wide grapheme at all. */
    CHECK(fixture_open(&f, 1, 1));
    CHECK(text(&f.canvas, 0, 0, WIDE, RED) == 2);
    CHECK(cell_is(&f.canvas, 0, 0, " ", 1, RED));
    CHECK(text(&f.canvas, 0, 0, "q", BLUE) == 1);
    CHECK(cell_is(&f.canvas, 0, 0, "q", 1, BLUE));
    CHECK(paired(&f.buffer));
    CHECK(fixture_close(&f));
    return true;
}

/* R20: overwriting half of a wide glyph, the other half possibly outside
 * the clip. */
static bool test_overwrite_halves(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 5, 1));
    CHECK(text(&f.canvas, 0, 0, WIDE WIDE, RED) == 4);
    CHECK(paired(&f.buffer));

    /* Second half overwritten inside the clip. */
    CHECK(text(&f.canvas, 1, 0, "a", BLUE) == 1);
    CHECK(cell_is(&f.canvas, 0, 0, " ", 1, RED));
    CHECK(cell_is(&f.canvas, 1, 0, "a", 1, BLUE));
    CHECK(cell_is(&f.canvas, 2, 0, WIDE, 2, RED));

    /* First half overwritten while the second lies outside the clip. */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 0, 0, 3, 1 });
    CHECK(text(&f.canvas, 2, 0, "b", BLUE) == 1);
    CHECK(cell_is(&f.canvas, 2, 0, "b", 1, BLUE));
    CHECK(cell_is(&f.canvas, 3, 0, " ", 1, RED));
    CHECK(paired(&f.buffer));

    /* Second half overwritten by a fill while the first is outside. */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 0, 0, 5, 1 });
    CHECK(text(&f.canvas, 3, 0, WIDE, RED) == 2);
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 4, 0, 1, 1 });
    ovd_canvas_fill(&f.canvas, (OvdRect){ 0, 0, 5, 1 }, BLUE);
    CHECK(cell_is(&f.canvas, 3, 0, " ", 1, RED));
    CHECK(cell_is(&f.canvas, 4, 0, " ", 1, BLUE));
    CHECK(cell_is(&f.canvas, 2, 0, "b", 1, BLUE));

    /* A cut wide glyph's space also repairs what it overwrites. */
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 0, 0, 5, 1 });
    CHECK(text(&f.canvas, 0, 0, WIDE, RED) == 2);
    ovd_canvas_set_clip(&f.canvas, (OvdRect){ 1, 0, 4, 1 });
    CHECK(text(&f.canvas, 0, 0, WIDE, BLUE) == 2);
    CHECK(cell_is(&f.canvas, 0, 0, " ", 1, RED));
    CHECK(cell_is(&f.canvas, 1, 0, " ", 1, BLUE));
    CHECK(paired(&f.buffer));
    CHECK(fixture_close(&f));
    return true;
}

/* FR-032, R17: no control byte reaches a cell. */
static bool test_untrusted_text(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 40, 3));
    CHECK(text(&f.canvas, 0, 0, "\x1b[31m", RED) == 5);
    CHECK(cell_is(&f.canvas, 0, 0, FFFD, 1, RED));
    CHECK(cell_is(&f.canvas, 1, 0, "[", 1, RED));

    /* NUL does not terminate; ill-formed UTF-8 counts per maximal subpart. */
    CHECK(ovd_canvas_text(&f.canvas, 0, 1, "a\0b", 3, RED) == 3);
    CHECK(cell_is(&f.canvas, 1, 1, FFFD, 1, RED) && cell_is(&f.canvas, 2, 1, "b", 1, RED));
    CHECK(text(&f.canvas, 3, 1, "\xF0\x80\x80", RED) == 3);
    CHECK(text(&f.canvas, 6, 1, "\xF0\x9F\x98", RED) == 1);
    /* A lone leading mark becomes U+FFFD carrying the marks after it. */
    CHECK(text(&f.canvas, 7, 1, ACUTE ACUTE, RED) == 1);
    CHECK(cell_is(&f.canvas, 7, 1, FFFD ACUTE, 1, RED));

    /* Every byte value: no cell text may contain a C0, DEL, or C1 byte. */
    char all[256];
    for (size_t i = 0; i < sizeof all; ++i) {
        all[i] = (char)i;
    }
    for (int32_t x = -3; x < 3; ++x) {
        CHECK(ovd_canvas_text(&f.canvas, x, 2, all, sizeof all, BLUE) > 0);
    }
    for (int32_t y = 0; y < 3; ++y) {
        for (int32_t x = 0; x < 40; ++x) {
            const OvdCell *cell = ovd_buffer_cell(&f.buffer, x, y);
            CHECK(cell->length <= OVD_GRAPHEME_MAX_BYTES && cell->text[cell->length] == '\0');
            for (size_t i = 0; i < cell->length; ++i) {
                const unsigned char byte = (unsigned char)cell->text[i];
                CHECK(byte >= 0x20 && byte != 0x7F);
            }
            /* C1 as UTF-8 is C2 80..C2 9F. */
            for (size_t i = 0; i + 1 < cell->length; ++i) {
                CHECK(!((unsigned char)cell->text[i] == 0xC2
                        && (unsigned char)cell->text[i + 1] < 0xA0));
            }
        }
    }
    CHECK(paired(&f.buffer));
    CHECK(fixture_close(&f));
    return true;
}

/* R22: colors keep only their kind's fields; unknown kinds read back as
 * DEFAULT; undefined attribute bits are dropped. */
static bool test_style_normalization(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 3, 1));
    const OvdStyle messy = {
        .fg = { .kind = OVD_COLOR_KIND_RGB, .index = 9, .r = 1, .g = 2, .b = 3 },
        .bg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 200, .r = 4, .g = 5, .b = 6 },
        .attrs = 0xFFFF,
    };
    const OvdStyle clean = {
        .fg = { .kind = OVD_COLOR_KIND_RGB, .r = 1, .g = 2, .b = 3 },
        .bg = { .kind = OVD_COLOR_KIND_INDEXED, .index = 200 },
        .attrs = OVD_ATTR_BOLD | OVD_ATTR_DIM | OVD_ATTR_ITALIC | OVD_ATTR_UNDERLINE
                 | OVD_ATTR_REVERSE | OVD_ATTR_STRIKETHROUGH,
    };
    CHECK(text(&f.canvas, 0, 0, "a", messy) == 1);
    CHECK(cell_is(&f.canvas, 0, 0, "a", 1, clean));

    const OvdStyle unknown = {
        .fg = { .kind = (OvdColorKind)7, .index = 1, .r = 2, .g = 3, .b = 4 },
        .bg = { .kind = OVD_COLOR_KIND_DEFAULT, .index = 5, .r = 6 },
        .attrs = (uint16_t)(1u << 9),
    };
    ovd_canvas_fill(&f.canvas, (OvdRect){ 1, 0, 1, 1 }, unknown);
    CHECK(cell_is(&f.canvas, 1, 0, " ", 1, OVD_STYLE_DEFAULT));
    CHECK(text(&f.canvas, 2, 0, WIDE, messy) == 2);
    CHECK(cell_is(&f.canvas, 2, 0, " ", 1, clean));
    CHECK(fixture_close(&f));
    return true;
}

/* FR-030: drawing never allocates. */
static bool test_no_allocation(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 8, 2));
    const size_t calls = f.alloc.allocate_calls;
    for (int i = 0; i < 100; ++i) {
        ovd_canvas_set_clip(&f.canvas, (OvdRect){ i % 3, 0, 8, 2 });
        ovd_canvas_fill(&f.canvas, (OvdRect){ -1, -1, 10, 10 }, RED);
        CHECK(text(&f.canvas, i % 9 - 1, i % 2, WIDE "x" WIDE, BLUE) == 5);
        OvdCellView view;
        CHECK(ovd_canvas_cell(&f.canvas, 0, 0, &view));
    }
    CHECK(f.alloc.allocate_calls == calls && f.alloc.release_calls == 0);
    CHECK(paired(&f.buffer));
    CHECK(fixture_close(&f));
    return true;
}

int main(void)
{
    typedef bool (*Test)(void);
    static const struct {
        const char *name;
        Test run;
    } tests[] = {
        { "request", test_request },
        { "adopt_and_release", test_adopt_and_release },
        { "buffer_pairing", test_buffer_pairing },
        { "closed", test_closed },
        { "open_and_clip", test_open_and_clip },
        { "text_basics", test_text_basics },
        { "cut_wide", test_cut_wide },
        { "overwrite_halves", test_overwrite_halves },
        { "untrusted_text", test_untrusted_text },
        { "style_normalization", test_style_normalization },
        { "no_allocation", test_no_allocation },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "canvas: %s failed\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        fprintf(stderr, "canvas: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("canvas: grid, clipping, text, wide glyphs, and closed canvas passed");
    return EXIT_SUCCESS;
}
