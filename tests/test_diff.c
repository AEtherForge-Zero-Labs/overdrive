/* Tests for src/diff.c (T027): FR-020, FR-021, FR-024, FR-025, SC-001 to
 * SC-003; research.md R8, R30, R33, R34, R38, R43.
 *
 * Every output is replayed on the strict oracle (tests/support), which
 * must end up showing exactly the frame as the depth shows it. Frames are
 * built with ovd_buffer_put from hand-built graphemes, and widths come
 * from the hand-written classifier below, so neither src/unicode nor
 * canvas.c is under test here (R38). */
#include "alloc.h"
#include "buffer.h"
#include "color.h"
#include "diff.h"
#include "output.h"
#include "support/test_alloc.h"
#include "vt_replay.h"

#include <overdrive/render.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDE "\xE7\x95\x8C"                   /* U+754C, wide */
#define ACUTE "\xCC\x81"                      /* U+0301, extender */
#define ZWJ "\xE2\x80\x8D"                    /* U+200D */
#define FAMILY "\xF0\x9F\x91\xA9" ZWJ "\xF0\x9F\x91\xA7" /* woman ZWJ girl, wide */
#define LONGEST "a" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE /* 15 bytes */

#define SYNC_ON "\x1b[?2026h"
#define SYNC_OFF "\x1b[?2026l"
#define RESET "\x1b[0m"
#define CLEAR "\x1b[2J"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

/* The classifier this test's code points need (R38): an independent,
 * hand-written reading of R16, not src/unicode. */
static TestWidth classify(uint32_t code_point)
{
    switch (code_point) {
    case 0x754C:  /* 界 */
    case 0x1F469: /* woman */
    case 0x1F467: /* girl */
        return TEST_WIDTH_WIDE;
    case 0x0301:
        return TEST_WIDTH_EXTEND;
    default:
        return TEST_WIDTH_NARROW;
    }
}

static OvdGrapheme grapheme(const char *text, uint8_t width)
{
    OvdGrapheme g = { .length = (uint8_t)strlen(text), .width = width };
    memcpy(g.bytes, text, g.length);
    return g;
}

static OvdStyle style(OvdColor fg, OvdColor bg, uint16_t attrs)
{
    return (OvdStyle){ .fg = fg, .bg = bg, .attrs = attrs };
}

static bool color_eq(OvdColor a, OvdColor b)
{
    return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

static bool style_eq(OvdStyle a, OvdStyle b)
{
    return color_eq(a.fg, b.fg) && color_eq(a.bg, b.bg) && a.attrs == b.attrs;
}

constexpr uint16_t ALL_ATTRS = OVD_ATTR_BOLD | OVD_ATTR_DIM | OVD_ATTR_ITALIC
                               | OVD_ATTR_UNDERLINE | OVD_ATTR_REVERSE
                               | OVD_ATTR_STRIKETHROUGH;

/* ---- Fixture ---------------------------------------------------------- */

typedef struct Fixture {
    TestAlloc alloc;
    OvdAllocator allocator;
    OvdOutput output;
} Fixture;

static void fixture_init(Fixture *f)
{
    memset(f, 0, sizeof *f);
    test_alloc_init(&f->alloc);
    f->allocator = test_alloc_allocator(&f->alloc);
}

static bool grid_make(Fixture *f, OvdBuffer *grid, int32_t cols, int32_t rows)
{
    *grid = (OvdBuffer){ 0 };
    const OvdSize size = { cols, rows };
    OvdBlockRequest request;
    OvdBlock block = { nullptr, 0 };
    if (ovd_buffer_request(size, &request) != OVD_OK
        || ovd_alloc_all(&f->allocator, &request, 1, &block) != OVD_OK) {
        return false;
    }
    ovd_buffer_adopt(grid, &f->allocator, &block, size);
    return true;
}

static bool output_make(Fixture *f, size_t capacity)
{
    OvdBlock block = { nullptr, 0 };
    if (ovd_alloc_block(&f->allocator, capacity, 1, &block) != OVD_OK) {
        return false;
    }
    ovd_output_adopt(&f->output, &f->allocator, block);
    return true;
}

/* Releases the output; true when nothing leaked and the allocator
 * contract held. Grids are released by the caller first. */
static bool fixture_done(Fixture *f)
{
    ovd_output_release(&f->output, &f->allocator);
    return f->alloc.live_blocks == 0 && f->alloc.violations == 0;
}

/* Copies src into dst (same size) through the grid's own writes. */
static void grid_copy(OvdBuffer *dst, const OvdBuffer *src)
{
    ovd_buffer_clear(dst);
    const OvdSize size = ovd_buffer_size(src);
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            const OvdCell *cell = ovd_buffer_cell(src, x, y);
            if (cell->width == 0) {
                continue;
            }
            OvdGrapheme g = { .length = cell->length, .width = cell->width };
            memcpy(g.bytes, cell->text, cell->length);
            ovd_buffer_put(dst, x, y, &g, cell->style);
        }
    }
}

/* ---- Checks ----------------------------------------------------------- */

static bool starts_with(const char *bytes, size_t length, const char *prefix)
{
    const size_t n = strlen(prefix);
    return length >= n && memcmp(bytes, prefix, n) == 0;
}

static bool ends_with(const char *bytes, size_t length, const char *suffix)
{
    const size_t n = strlen(suffix);
    return length >= n && memcmp(bytes + length - n, suffix, n) == 0;
}

static bool contains(const char *bytes, size_t length, const char *needle)
{
    const size_t n = strlen(needle);
    for (size_t i = 0; i + n <= length; ++i) {
        if (memcmp(bytes + i, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

/* The number of CUP sequences (ESC [ digits ; digits H) in the bytes. */
static size_t count_cup(const char *bytes, size_t length)
{
    size_t count = 0;
    for (size_t i = 0; i + 1 < length; ++i) {
        if (bytes[i] != '\x1b' || bytes[i + 1] != '[') {
            continue;
        }
        size_t j = i + 2;
        while (j < length && bytes[j] >= '0' && bytes[j] <= '9') {
            ++j;
        }
        if (j == i + 2 || j >= length || bytes[j] != ';') {
            continue;
        }
        const size_t k = ++j;
        while (j < length && bytes[j] >= '0' && bytes[j] <= '9') {
            ++j;
        }
        if (j > k && j < length && bytes[j] == 'H') {
            ++count;
        }
    }
    return count;
}

/* R34's framing for a non-empty output. */
static bool framed(const char *bytes, size_t length, bool synchronized, bool full)
{
    if (synchronized) {
        CHECK(starts_with(bytes, length, SYNC_ON));
        CHECK(ends_with(bytes, length, SYNC_OFF));
        bytes += strlen(SYNC_ON);
        length -= strlen(SYNC_ON) + strlen(SYNC_OFF);
    }
    CHECK(!contains(bytes, length, "\x1b[?2026"));
    if (full) {
        CHECK(starts_with(bytes, length, RESET CLEAR));
    }
    CHECK(!contains(full ? bytes + strlen(RESET CLEAR) : bytes,
                    full ? length - strlen(RESET CLEAR) : length, CLEAR));
    CHECK(ends_with(bytes, length, RESET));
    return true;
}

/* frame as depth shows it, in the oracle's screen form. */
static TestScreen *shown(const OvdBuffer *frame, OvdColorDepth depth)
{
    const OvdSize size = ovd_buffer_size(frame);
    TestScreen *screen = test_screen_create(size.cols, size.rows);
    if (screen == nullptr) {
        return nullptr;
    }
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            const OvdCell *cell = ovd_buffer_cell(frame, x, y);
            if (cell->width == 0) {
                continue;
            }
            const OvdStyle s = style(ovd_color_downsample(cell->style.fg, depth),
                                     ovd_color_downsample(cell->style.bg, depth),
                                     (uint16_t)(cell->style.attrs & ALL_ATTRS));
            test_screen_put(screen, x, y, cell->text, cell->length, cell->width, s);
        }
    }
    return screen;
}

/* One frame: renders, checks the bound and the framing, replays the
 * output as one write, and compares the oracle's screen with the frame.
 * *length, when not nullptr, receives the output's length. */
static bool step(Fixture *f, VtReplay *vt, const OvdBuffer *screen, const OvdBuffer *frame,
                 OvdColorDepth depth, bool synchronized, size_t *length)
{
    ovd_output_reset(&f->output);
    CHECK(ovd_diff_render(screen, frame, depth, synchronized, &f->output) == OVD_OK);
    const char *bytes = ovd_output_bytes(&f->output);
    const size_t n = ovd_output_length(&f->output);
    size_t bound = 0;
    CHECK(ovd_diff_bound(ovd_buffer_size(frame), synchronized, &bound) == OVD_OK);
    CHECK(n <= bound);
    if (screen == nullptr || n > 0) {
        CHECK(framed(bytes, n, synchronized, screen == nullptr));
    }
    if (!vt_replay_feed(vt, bytes, n)) {
        fprintf(stderr, "replay rejected the output: %s\n", vt_replay_error(vt));
        return false;
    }
    TestScreen *expected = shown(frame, depth);
    CHECK(expected != nullptr);
    char message[512];
    const bool equal = test_screen_equal(expected, vt_replay_screen(vt), message, sizeof message);
    test_screen_destroy(expected);
    if (!equal) {
        char dump[2048];
        test_screen_dump(vt_replay_screen(vt), dump, sizeof dump);
        fprintf(stderr, "screens differ: %s\nactual:\n%s", message, dump);
        return false;
    }
    const VtReplayState state = vt_replay_state(vt);
    CHECK(!state.synchronized);
    CHECK(style_eq(state.style, OVD_STYLE_DEFAULT));
    if (length != nullptr) {
        *length = n;
    }
    return true;
}

static bool output_is(const Fixture *f, const char *expected)
{
    const size_t n = strlen(expected);
    if (ovd_output_length(&f->output) != n
        || memcmp(ovd_output_bytes(&f->output), expected, n) != 0) {
        fprintf(stderr, "output (%zu bytes) is not the expected %zu bytes\n",
                ovd_output_length(&f->output), n);
        return false;
    }
    return true;
}

/* ---- Tests ------------------------------------------------------------ */

static bool test_bound(void)
{
    size_t bytes = 7;
    CHECK(ovd_diff_bound((OvdSize){ 80, 24 }, true, &bytes) == OVD_OK);
    CHECK(bytes == 16 + 8 + 4 + 1920u * (4 + 2 + 2 + 48 + 15));
    CHECK(ovd_diff_bound((OvdSize){ 80, 24 }, false, &bytes) == OVD_OK);
    CHECK(bytes == 8 + 4 + 1920u * (4 + 2 + 2 + 48 + 15));
    CHECK(ovd_diff_bound((OvdSize){ 1, 1 }, false, &bytes) == OVD_OK);
    CHECK(bytes == 8 + 4 + (4 + 1 + 1 + 48 + 15));
    CHECK(ovd_diff_bound((OvdSize){ 999, 1000 }, false, &bytes) == OVD_OK);
    CHECK(bytes == 8 + 4 + 999000u * (4 + 4 + 3 + 48 + 15));

    bytes = 7;
    CHECK(ovd_diff_bound((OvdSize){ 80, 24 }, false, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_bound((OvdSize){ 0, 24 }, false, &bytes) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_bound((OvdSize){ 80, -1 }, false, &bytes) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_bound((OvdSize){ INT32_MAX, INT32_MAX }, true, &bytes)
          == OVD_ERROR_ALLOCATION);
    CHECK(bytes == 7);
    return true;
}

static bool test_invalid_arguments(void)
{
    Fixture f;
    fixture_init(&f);
    OvdBuffer a, b;
    CHECK(grid_make(&f, &a, 3, 2));
    CHECK(grid_make(&f, &b, 2, 3));
    CHECK(output_make(&f, 256));
    CHECK(ovd_output_append(&f.output, "xy", 2));
    const OvdBuffer empty = { 0 };

    CHECK(ovd_diff_render(nullptr, nullptr, OVD_COLOR_DEPTH_TRUECOLOR, false, &f.output)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_render(nullptr, &a, OVD_COLOR_DEPTH_TRUECOLOR, false, nullptr)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_render(nullptr, &empty, OVD_COLOR_DEPTH_TRUECOLOR, false, &f.output)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_render(&b, &a, OVD_COLOR_DEPTH_TRUECOLOR, false, &f.output)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_render(&empty, &a, OVD_COLOR_DEPTH_TRUECOLOR, false, &f.output)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_diff_render(nullptr, &a, (OvdColorDepth)4, false, &f.output)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(output_is(&f, "xy"));

    ovd_buffer_release(&a, &f.allocator);
    ovd_buffer_release(&b, &f.allocator);
    CHECK(fixture_done(&f));
    return true;
}

/* Exact bytes at every depth, then a known-screen change and an
 * unchanged frame. */
static bool test_exact_bytes(void)
{
    static const struct {
        OvdColorDepth depth;
        const char *sgr;
    } cases[] = {
        { OVD_COLOR_DEPTH_TRUECOLOR, "\x1b[38;2;255;0;0m" },
        { OVD_COLOR_DEPTH_256, "\x1b[38;5;196m" },
        { OVD_COLOR_DEPTH_16, "\x1b[91m" },
        { OVD_COLOR_DEPTH_NONE, "" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        for (int sync = 0; sync < 2; ++sync) {
            Fixture f;
            fixture_init(&f);
            OvdBuffer first, second;
            CHECK(grid_make(&f, &first, 3, 1));
            CHECK(grid_make(&f, &second, 3, 1));
            CHECK(output_make(&f, 4096));
            VtReplay *vt = vt_replay_create(3, 1, classify);
            CHECK(vt != nullptr);

            const OvdGrapheme a = grapheme("a", 1);
            const OvdGrapheme b = grapheme("b", 1);
            const OvdStyle red = style(ovd_color_rgb(255, 0, 0), OVD_COLOR_DEFAULT, 0);
            CHECK(ovd_buffer_put(&first, 0, 0, &a, red));
            CHECK(step(&f, vt, nullptr, &first, cases[i].depth, sync, nullptr));
            char expected[128];
            snprintf(expected, sizeof expected, "%s" RESET CLEAR "\x1b[1;1H%sa" RESET "%s",
                     sync ? SYNC_ON : "", cases[i].sgr, sync ? SYNC_OFF : "");
            CHECK(output_is(&f, expected));

            grid_copy(&second, &first);
            CHECK(ovd_buffer_put(&second, 1, 0, &b, OVD_STYLE_DEFAULT));
            CHECK(step(&f, vt, &first, &second, cases[i].depth, sync, nullptr));
            snprintf(expected, sizeof expected, "%s\x1b[1;2Hb" RESET "%s",
                     sync ? SYNC_ON : "", sync ? SYNC_OFF : "");
            CHECK(output_is(&f, expected));

            /* SC-002: an unchanged frame writes nothing, even synchronized. */
            size_t n = 1;
            CHECK(step(&f, vt, &second, &second, cases[i].depth, sync, &n));
            CHECK(n == 0);

            vt_replay_destroy(vt);
            ovd_buffer_release(&first, &f.allocator);
            ovd_buffer_release(&second, &f.allocator);
            CHECK(fixture_done(&f));
        }
    }
    return true;
}

/* A full redraw writes only the non-default cells; runs share one CUP;
 * a cell in the last column and the bottom-right cell are safe (R33). */
static bool test_runs_and_redraw(void)
{
    Fixture f;
    fixture_init(&f);
    OvdBuffer blank, frame;
    CHECK(grid_make(&f, &blank, 6, 2));
    CHECK(grid_make(&f, &frame, 6, 2));
    CHECK(output_make(&f, 4096));
    VtReplay *vt = vt_replay_create(6, 2, classify);
    CHECK(vt != nullptr);

    size_t n = 0;
    CHECK(step(&f, vt, nullptr, &blank, OVD_COLOR_DEPTH_TRUECOLOR, false, &n));
    CHECK(output_is(&f, RESET CLEAR RESET));

    const char *const text = "abc";
    for (int32_t x = 0; x < 3; ++x) {
        const OvdGrapheme g = grapheme((char[]){ text[x], 0 }, 1);
        CHECK(ovd_buffer_put(&frame, x, 0, &g, OVD_STYLE_DEFAULT));
    }
    const OvdGrapheme d = grapheme("d", 1);
    const OvdGrapheme e = grapheme("e", 1);
    const OvdGrapheme z = grapheme("z", 1);
    const OvdGrapheme space = grapheme(" ", 1);
    CHECK(ovd_buffer_put(&frame, 5, 0, &d, OVD_STYLE_DEFAULT));
    CHECK(ovd_buffer_put(&frame, 0, 1, &e, OVD_STYLE_DEFAULT));
    CHECK(ovd_buffer_put(&frame, 5, 1, &z, OVD_STYLE_DEFAULT));
    /* A space in a color is not a default cell. */
    CHECK(ovd_buffer_put(&frame, 2, 1, &space,
                         style(OVD_COLOR_DEFAULT, ovd_color_indexed(4), 0)));

    CHECK(step(&f, vt, &blank, &frame, OVD_COLOR_DEPTH_TRUECOLOR, false, &n));
    CHECK(output_is(&f, "\x1b[1;1Habc\x1b[1;6Hd\x1b[2;1He\x1b[2;3H\x1b[44m \x1b[2;6H"
                        "\x1b[49mz" RESET));

    /* The same frame on an unknown screen: the prefix, then the same cells. */
    CHECK(step(&f, vt, nullptr, &frame, OVD_COLOR_DEPTH_TRUECOLOR, false, &n));
    CHECK(count_cup(ovd_output_bytes(&f.output), n) == 5);

    vt_replay_destroy(vt);
    ovd_buffer_release(&blank, &f.allocator);
    ovd_buffer_release(&frame, &f.allocator);
    CHECK(fixture_done(&f));
    return true;
}

/* After a cell ending in ZWJ, the next glyph follows a CUP (R38). */
static bool test_zwj_final(void)
{
    Fixture f;
    fixture_init(&f);
    OvdBuffer frame;
    CHECK(grid_make(&f, &frame, 4, 1));
    CHECK(output_make(&f, 4096));
    VtReplay *vt = vt_replay_create(4, 1, classify);
    CHECK(vt != nullptr);

    const OvdGrapheme joined = grapheme("a" ZWJ, 1);
    const OvdGrapheme b = grapheme("b", 1);
    CHECK(ovd_buffer_put(&frame, 0, 0, &joined, OVD_STYLE_DEFAULT));
    CHECK(ovd_buffer_put(&frame, 1, 0, &b, OVD_STYLE_DEFAULT));
    CHECK(step(&f, vt, nullptr, &frame, OVD_COLOR_DEPTH_TRUECOLOR, false, nullptr));
    CHECK(output_is(&f, RESET CLEAR "\x1b[1;1Ha" ZWJ "\x1b[1;2Hb" RESET));

    vt_replay_destroy(vt);
    ovd_buffer_release(&frame, &f.allocator);
    CHECK(fixture_done(&f));
    return true;
}

/* Splitting a wide glyph on screen rewrites both halves. */
static bool test_wide_splits(void)
{
    Fixture f;
    fixture_init(&f);
    OvdBuffer grids[2];
    CHECK(grid_make(&f, &grids[0], 4, 2));
    CHECK(grid_make(&f, &grids[1], 4, 2));
    CHECK(output_make(&f, 4096));
    VtReplay *vt = vt_replay_create(4, 2, classify);
    CHECK(vt != nullptr);

    const OvdGrapheme wide = grapheme(WIDE, 2);
    const OvdGrapheme q = grapheme("q", 1);
    const OvdStyle green = style(ovd_color_indexed(2), OVD_COLOR_DEFAULT, OVD_ATTR_ITALIC);
    /* Each frame's cells, in order: x, y, grapheme. */
    static const struct {
        int32_t x;
        int32_t y;
        bool wide;
    } frames[][3] = {
        { { 1, 0, true }, { 2, 1, true }, { -1, 0, false } },  /* 界 at 1, and at the end of row 1 */
        { { 2, 0, false }, { -1, 0, false } },                 /* q over its second half */
        { { 0, 0, true }, { 2, 0, false }, { -1, 0, false } }, /* 界 at 0, q kept */
        { { 1, 0, true }, { 3, 1, false }, { -1, 0, false } }, /* 界 moves right by one */
        { { 0, 1, true }, { 2, 1, true }, { -1, 0, false } },  /* two wide, touching */
        { { 1, 1, true }, { -1, 0, false } },                  /* across both of them */
    };
    const OvdBuffer *screen = nullptr;
    for (size_t i = 0; i < sizeof frames / sizeof frames[0]; ++i) {
        OvdBuffer *frame = &grids[i % 2];
        ovd_buffer_clear(frame);
        for (size_t j = 0; j < 3 && frames[i][j].x >= 0; ++j) {
            CHECK(ovd_buffer_put(frame, frames[i][j].x, frames[i][j].y,
                                 frames[i][j].wide ? &wide : &q, green));
        }
        CHECK(step(&f, vt, screen, frame, OVD_COLOR_DEPTH_256, i % 2 == 1, nullptr));
        screen = frame;
    }

    vt_replay_destroy(vt);
    ovd_buffer_release(&grids[0], &f.allocator);
    ovd_buffer_release(&grids[1], &f.allocator);
    CHECK(fixture_done(&f));
    return true;
}

/* Width is part of a cell: the same text and style at another width is a
 * change. The oracle measures with its own classifier, so it cannot show
 * this; the bytes are checked directly. */
static bool test_width_compared(void)
{
    Fixture f;
    fixture_init(&f);
    OvdBuffer narrow, wide;
    CHECK(grid_make(&f, &narrow, 3, 1));
    CHECK(grid_make(&f, &wide, 3, 1));
    CHECK(output_make(&f, 4096));
    const OvdGrapheme one = grapheme("a", 1);
    const OvdGrapheme two = grapheme("a", 2);
    CHECK(ovd_buffer_put(&narrow, 0, 0, &one, OVD_STYLE_DEFAULT));
    CHECK(ovd_buffer_put(&narrow, 1, 0, &one, OVD_STYLE_DEFAULT));
    CHECK(ovd_buffer_put(&wide, 0, 0, &two, OVD_STYLE_DEFAULT));
    CHECK(ovd_diff_render(&narrow, &wide, OVD_COLOR_DEPTH_TRUECOLOR, false, &f.output)
          == OVD_OK);
    CHECK(output_is(&f, "\x1b[1;1Ha" RESET));

    ovd_buffer_release(&narrow, &f.allocator);
    ovd_buffer_release(&wide, &f.allocator);
    CHECK(fixture_done(&f));
    return true;
}

/* Combining marks, a wide ZWJ sequence, and 15-byte graphemes. */
static bool test_long_graphemes(void)
{
    Fixture f;
    fixture_init(&f);
    OvdBuffer blank, frame;
    CHECK(grid_make(&f, &blank, 5, 2));
    CHECK(grid_make(&f, &frame, 5, 2));
    CHECK(output_make(&f, 4096));
    VtReplay *vt = vt_replay_create(5, 2, classify);
    CHECK(vt != nullptr);

    const OvdGrapheme longest = grapheme(LONGEST, 1);
    const OvdGrapheme family = grapheme(FAMILY, 2);
    const OvdGrapheme accented = grapheme("e" ACUTE, 1);
    CHECK(longest.length == 15);
    for (int32_t x = 0; x < 5; ++x) {
        CHECK(ovd_buffer_put(&frame, x, 0, &longest, OVD_STYLE_DEFAULT));
    }
    CHECK(ovd_buffer_put(&frame, 0, 1, &accented, OVD_STYLE_DEFAULT));
    CHECK(ovd_buffer_put(&frame, 1, 1, &family, OVD_STYLE_DEFAULT));
    CHECK(ovd_buffer_put(&frame, 3, 1, &family, OVD_STYLE_DEFAULT));

    CHECK(step(&f, vt, nullptr, &blank, OVD_COLOR_DEPTH_TRUECOLOR, false, nullptr));
    CHECK(step(&f, vt, &blank, &frame, OVD_COLOR_DEPTH_TRUECOLOR, false, nullptr));
    CHECK(count_cup(ovd_output_bytes(&f.output), ovd_output_length(&f.output)) == 2);
    CHECK(step(&f, vt, nullptr, &frame, OVD_COLOR_DEPTH_TRUECOLOR, true, nullptr));
    CHECK(step(&f, vt, &frame, &blank, OVD_COLOR_DEPTH_TRUECOLOR, true, nullptr));

    vt_replay_destroy(vt);
    ovd_buffer_release(&blank, &f.allocator);
    ovd_buffer_release(&frame, &f.allocator);
    CHECK(fixture_done(&f));
    return true;
}

/* SC-003: one narrow cell, both colors RGB, no attributes, no sync, at
 * 999x999: at most 64 bytes, at the corners and in the middle. */
static bool test_one_cell_budget(void)
{
    Fixture f;
    fixture_init(&f);
    OvdBuffer blank, frame;
    CHECK(grid_make(&f, &blank, 999, 999));
    CHECK(grid_make(&f, &frame, 999, 999));
    CHECK(output_make(&f, 256));
    VtReplay *vt = vt_replay_create(999, 999, classify);
    CHECK(vt != nullptr);

    /* The screen starts blank and known: the oracle's own start, plus the
     * known cursor-free state a previous frame leaves. */
    const OvdGrapheme glyph = grapheme("\xE2\x82\xAC", 1); /* U+20AC, 3 bytes */
    const OvdStyle rgb = style(ovd_color_rgb(255, 255, 255), ovd_color_rgb(200, 201, 202), 0);
    static const int32_t points[][2] = { { 998, 998 }, { 0, 0 }, { 998, 0 }, { 0, 998 },
                                         { 499, 500 } };
    for (size_t i = 0; i < sizeof points / sizeof points[0]; ++i) {
        ovd_buffer_clear(&frame);
        CHECK(ovd_buffer_put(&frame, points[i][0], points[i][1], &glyph, rgb));
        size_t n = 0;
        CHECK(step(&f, vt, &blank, &frame, OVD_COLOR_DEPTH_TRUECOLOR, false, &n));
        CHECK(n <= 64);
        /* Back to blank, so the next point starts from a blank screen. */
        CHECK(step(&f, vt, &frame, &blank, OVD_COLOR_DEPTH_TRUECOLOR, false, &n));
        CHECK(n <= 64);
    }
    ovd_buffer_clear(&frame);
    CHECK(ovd_buffer_put(&frame, 998, 998, &glyph, rgb));
    CHECK(step(&f, vt, &blank, &frame, OVD_COLOR_DEPTH_TRUECOLOR, false, nullptr));
    CHECK(output_is(&f, "\x1b[999;999H\x1b[38;2;255;255;255;48;2;200;201;202m\xE2\x82\xAC"
                        RESET));

    vt_replay_destroy(vt);
    ovd_buffer_release(&blank, &f.allocator);
    ovd_buffer_release(&frame, &f.allocator);
    CHECK(fixture_done(&f));
    return true;
}

/* R30: the worst frames fit an output of exactly ovd_diff_bound bytes;
 * one byte less than the output needs fails with OVD_ERROR_IO and leaves
 * the output empty. */
static bool test_adversarial_bound(void)
{
    const int32_t cols = 9;
    const int32_t rows = 3;
    for (int sync = 0; sync < 2; ++sync) {
        Fixture f;
        fixture_init(&f);
        OvdBuffer base, checker;
        CHECK(grid_make(&f, &base, cols, rows));
        CHECK(grid_make(&f, &checker, cols, rows));
        size_t bound = 0;
        CHECK(ovd_diff_bound((OvdSize){ cols, rows }, sync, &bound) == OVD_OK);
        CHECK(output_make(&f, bound));
        VtReplay *vt = vt_replay_create(cols, rows, classify);
        CHECK(vt != nullptr);

        const OvdColor white = ovd_color_rgb(255, 255, 255);
        const OvdGrapheme longest = grapheme(LONGEST, 1);
        const OvdGrapheme other = grapheme("b" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE, 1);
        /* Every cell: 15 bytes, all attributes, two RGB colors. Every
         * other cell then changes, so each needs a CUP, and its style
         * drops one attribute from the last, so each SGR is a 48-byte
         * reset. */
        for (int32_t y = 0; y < rows; ++y) {
            for (int32_t x = 0; x < cols; ++x) {
                CHECK(ovd_buffer_put(&base, x, y, &longest, style(white, white, ALL_ATTRS)));
                const int32_t i = y * cols + x;
                const uint16_t drop = (i / 2) % 2 == 0 ? OVD_ATTR_BOLD : OVD_ATTR_DIM;
                if (i % 2 == 0) {
                    CHECK(ovd_buffer_put(&checker, x, y, &other,
                                         style(white, white, (uint16_t)(ALL_ATTRS & ~drop))));
                } else {
                    CHECK(ovd_buffer_put(&checker, x, y, &longest,
                                         style(white, white, ALL_ATTRS)));
                }
            }
        }
        size_t n = 0;
        CHECK(step(&f, vt, nullptr, &base, OVD_COLOR_DEPTH_TRUECOLOR, sync, &n));
        CHECK(step(&f, vt, &base, &checker, OVD_COLOR_DEPTH_TRUECOLOR, sync, &n));
        CHECK(count_cup(ovd_output_bytes(&f.output), n) == (size_t)(cols * rows + 1) / 2);
        CHECK(step(&f, vt, nullptr, &checker, OVD_COLOR_DEPTH_TRUECOLOR, sync, &n));

        /* An output one byte short of this frame's needs. */
        ovd_output_release(&f.output, &f.allocator);
        CHECK(output_make(&f, n - 1));
        CHECK(ovd_diff_render(nullptr, &checker, OVD_COLOR_DEPTH_TRUECOLOR, sync, &f.output)
              == OVD_ERROR_IO);
        CHECK(ovd_output_length(&f.output) == 0);

        vt_replay_destroy(vt);
        ovd_buffer_release(&base, &f.allocator);
        ovd_buffer_release(&checker, &f.allocator);
        CHECK(fixture_done(&f));
    }
    return true;
}

/* ---- Random frame sequences (SC-001) ---------------------------------- */

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static OvdColor random_color(uint32_t *state)
{
    switch (next_random(state) % 4) {
    case 0:
        return OVD_COLOR_DEFAULT;
    case 1:
        return ovd_color_indexed((uint8_t)(next_random(state) % 16));
    case 2:
        return ovd_color_indexed((uint8_t)next_random(state));
    default:
        return ovd_color_hex(next_random(state));
    }
}

static OvdStyle random_style(uint32_t *state)
{
    if (next_random(state) % 3 == 0) {
        return OVD_STYLE_DEFAULT;
    }
    return style(random_color(state), random_color(state),
                 (uint16_t)(next_random(state) & ALL_ATTRS));
}

static void random_put(OvdBuffer *grid, uint32_t *state)
{
    static const struct {
        const char *text;
        uint8_t width;
    } glyphs[] = {
        { "a", 1 },     { "Z", 1 },          { " ", 1 },   { "~", 1 },
        { WIDE, 2 },    { "e" ACUTE, 1 },    { "a" ZWJ, 1 }, { LONGEST, 1 },
        { FAMILY, 2 },  { "\xC3\xA9", 1 },
    };
    const OvdSize size = ovd_buffer_size(grid);
    const size_t pick = next_random(state) % (sizeof glyphs / sizeof glyphs[0]);
    const OvdGrapheme g = grapheme(glyphs[pick].text, glyphs[pick].width);
    const int32_t x = (int32_t)(next_random(state) % (uint32_t)size.cols);
    const int32_t y = (int32_t)(next_random(state) % (uint32_t)size.rows);
    /* A wide glyph in the last column is refused; that is fine here. */
    ovd_buffer_put(grid, x, y, &g, random_style(state));
}

static bool test_random_sequences(void)
{
    static const OvdSize sizes[] = { { 1, 1 }, { 2, 1 }, { 1, 3 }, { 7, 4 }, { 13, 5 } };
    static const OvdColorDepth depths[] = { OVD_COLOR_DEPTH_TRUECOLOR, OVD_COLOR_DEPTH_256,
                                            OVD_COLOR_DEPTH_16, OVD_COLOR_DEPTH_NONE };
    uint32_t state = 0x9E3779B9u;
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) {
        for (size_t d = 0; d < sizeof depths / sizeof depths[0]; ++d) {
            Fixture f;
            fixture_init(&f);
            OvdBuffer grids[2];
            CHECK(grid_make(&f, &grids[0], sizes[s].cols, sizes[s].rows));
            CHECK(grid_make(&f, &grids[1], sizes[s].cols, sizes[s].rows));
            size_t bound = 0;
            CHECK(ovd_diff_bound(sizes[s], true, &bound) == OVD_OK);
            CHECK(output_make(&f, bound));
            VtReplay *vt = vt_replay_create(sizes[s].cols, sizes[s].rows, classify);
            CHECK(vt != nullptr);

            const OvdBuffer *screen = nullptr;
            for (int i = 0; i < 60; ++i) {
                OvdBuffer *frame = &grids[i % 2];
                const uint32_t kind = next_random(&state) % 8;
                const int32_t cells = sizes[s].cols * sizes[s].rows;
                const bool unchanged = kind == 7 && screen != nullptr;
                if (kind < 2 || screen == nullptr) {
                    /* A fresh frame. */
                    ovd_buffer_clear(frame);
                    const int32_t puts = (int32_t)(next_random(&state) % (uint32_t)(2 * cells + 1));
                    for (int32_t k = 0; k < puts; ++k) {
                        random_put(frame, &state);
                    }
                } else {
                    /* The screen plus a few changes; kind 7 changes nothing. */
                    grid_copy(frame, screen);
                    const uint32_t puts = kind == 7 ? 0 : 1 + next_random(&state) % 3;
                    for (uint32_t k = 0; k < puts; ++k) {
                        random_put(frame, &state);
                    }
                }
                const bool unknown = next_random(&state) % 10 == 0;
                const bool sync = next_random(&state) % 2 == 0;
                size_t n = 0;
                if (!step(&f, vt, unknown ? nullptr : screen, frame, depths[d], sync, &n)) {
                    fprintf(stderr, "random: size %dx%d, depth %d, frame %d\n",
                            (int)sizes[s].cols, (int)sizes[s].rows, (int)depths[d], i);
                    return false;
                }
                if (unchanged && !unknown) {
                    CHECK(n == 0);
                }
                screen = frame;
            }

            vt_replay_destroy(vt);
            ovd_buffer_release(&grids[0], &f.allocator);
            ovd_buffer_release(&grids[1], &f.allocator);
            CHECK(fixture_done(&f));
        }
    }
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "bound", test_bound },
        { "invalid_arguments", test_invalid_arguments },
        { "exact_bytes", test_exact_bytes },
        { "runs_and_redraw", test_runs_and_redraw },
        { "zwj_final", test_zwj_final },
        { "wide_splits", test_wide_splits },
        { "width_compared", test_width_compared },
        { "long_graphemes", test_long_graphemes },
        { "one_cell_budget", test_one_cell_budget },
        { "adversarial_bound", test_adversarial_bound },
        { "random_sequences", test_random_sequences },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures == 0) {
        printf("diff: bound, arguments, depths, runs, ZWJ, wide splits, width, long graphemes, "
               "SC-003 budget, adversarial bound, random sequences passed\n");
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
