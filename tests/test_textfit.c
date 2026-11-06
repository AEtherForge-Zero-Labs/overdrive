/* Tests for src/textfit.c (004 R308, R318; data-model.md, "Internal: text
 * fitting"): what text fits into a width, where it is cut, the U+2026
 * mark, and the padding; and that drawing fills exactly the width. The
 * measured width must equal what ovd_canvas_text spans, since both use
 * the same grapheme iterator (R308). Also the internal widget helpers of
 * src/widget.c, which no public call reaches yet: the clip narrowing
 * (R309) and the style combination (R310). */
#include "textfit.h"
#include "widget.h"

#include "support/canvas_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDE "\xE4\xB8\xAD" /* U+4E2D, East_Asian_Width W */
#define ACUTE "\xCC\x81"    /* U+0301 COMBINING ACUTE ACCENT, Mn */
#define ELLIPSIS "\xE2\x80\xA6"

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

typedef struct Case {
    const char *name;
    const char *text; /* nullptr allowed */
    size_t length;
    int32_t width;
    OvdAlign align;
    OvdFit expected;
} Case;

/* A string literal and its length, so embedded bytes count. */
#define TEXT(literal) literal, sizeof(literal) - 1

static const Case cases[] = {
    /* Text that fits is aligned. */
    { "fits, left", TEXT("abc"), 5, OVD_ALIGN_LEFT, { 3, 3, false, 0, 2 } },
    { "fits, right", TEXT("abc"), 5, OVD_ALIGN_RIGHT, { 3, 3, false, 2, 0 } },
    { "fits exactly", TEXT("abc"), 3, OVD_ALIGN_RIGHT, { 3, 3, false, 0, 0 } },
    { "wide fits exactly", TEXT(WIDE), 2, OVD_ALIGN_LEFT, { 3, 2, false, 0, 0 } },
    { "align out of range reads left", TEXT("ab"), 4, (OvdAlign)7, { 2, 2, false, 0, 2 } },
    /* Cut text keeps graphemes within w - 1 and adds the mark. */
    { "cut", TEXT("abcdef"), 4, OVD_ALIGN_LEFT, { 3, 3, true, 0, 0 } },
    { "cut fills from the left when aligned right", TEXT("abcdef"), 4, OVD_ALIGN_RIGHT,
      { 3, 3, true, 0, 0 } },
    { "cut before a wide grapheme at the end", TEXT("ab" WIDE), 3, OVD_ALIGN_LEFT,
      { 2, 2, true, 0, 0 } },
    { "a wide grapheme that would be cut is left out", TEXT("a" WIDE "b"), 3, OVD_ALIGN_LEFT,
      { 1, 1, true, 0, 1 } },
    { "w = 2 keeps one narrow grapheme", TEXT("abc"), 2, OVD_ALIGN_LEFT, { 1, 1, true, 0, 0 } },
    { "w = 2 with a wide first grapheme", TEXT(WIDE "b"), 2, OVD_ALIGN_LEFT,
      { 0, 0, true, 0, 1 } },
    /* w = 1, w = 0, and a negative w. */
    { "w = 1, narrow first", TEXT("ab"), 1, OVD_ALIGN_RIGHT, { 1, 1, false, 0, 0 } },
    { "w = 1, wide first", TEXT(WIDE "b"), 1, OVD_ALIGN_LEFT, { 0, 0, false, 0, 1 } },
    { "w = 1, fits", TEXT("a"), 1, OVD_ALIGN_LEFT, { 1, 1, false, 0, 0 } },
    { "w = 0", TEXT("abc"), 0, OVD_ALIGN_LEFT, { 0, 0, false, 0, 0 } },
    { "negative w reads as 0", TEXT("abc"), -5, OVD_ALIGN_LEFT, { 0, 0, false, 0, 0 } },
    { "INT32_MIN w reads as 0", TEXT("abc"), INT32_MIN, OVD_ALIGN_RIGHT, { 0, 0, false, 0, 0 } },
    /* Graphemes as the canvas segments them. */
    { "base plus mark is one grapheme", TEXT("e" ACUTE "x"), 2, OVD_ALIGN_LEFT,
      { 4, 2, false, 0, 0 } },
    { "base plus mark is kept whole when cut", TEXT("e" ACUTE "xyz"), 3, OVD_ALIGN_LEFT,
      { 4, 2, true, 0, 0 } },
    { "a 16-byte cluster is one grapheme, 1 wide",
      TEXT("a" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE "\xE2\x80\x8B"), 3, OVD_ALIGN_LEFT,
      { 16, 1, false, 0, 2 } },
    { "each maximal subpart is 1 wide", TEXT("\xF0\x80\x80"), 3, OVD_ALIGN_LEFT,
      { 3, 3, false, 0, 0 } },
    { "a truncated sequence is 1 wide", TEXT("\xF0\x9F\x98"), 3, OVD_ALIGN_LEFT,
      { 3, 1, false, 0, 2 } },
    { "a control is 1 wide", TEXT("a\nb"), 3, OVD_ALIGN_LEFT, { 3, 3, false, 0, 0 } },
    /* Empty text. */
    { "empty, left", TEXT(""), 3, OVD_ALIGN_LEFT, { 0, 0, false, 0, 3 } },
    { "empty, right", TEXT(""), 3, OVD_ALIGN_RIGHT, { 0, 0, false, 3, 0 } },
    { "nullptr with a length is empty", nullptr, 5, 3, OVD_ALIGN_LEFT, { 0, 0, false, 0, 3 } },
};

static bool same_fit(OvdFit a, OvdFit b)
{
    return a.length == b.length && a.width == b.width && a.mark == b.mark
        && a.pad_left == b.pad_left && a.pad_right == b.pad_right;
}

static void test_measure(void)
{
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const Case *c = &cases[i];
        const OvdFit fit = ovd_textfit_measure(c->text, c->length, c->width, c->align);
        if (!same_fit(fit, c->expected)) {
            fprintf(stderr,
                    "measure \"%s\": got {%zu, %d, %d, %d, %d}, expected {%zu, %d, %d, %d, %d}\n",
                    c->name, fit.length, (int)fit.width, (int)fit.mark, (int)fit.pad_left,
                    (int)fit.pad_right, c->expected.length, (int)c->expected.width,
                    (int)c->expected.mark, (int)c->expected.pad_left,
                    (int)c->expected.pad_right);
            ++failures;
        }
        /* The parts always add up to the width (0 for w <= 0). */
        const int64_t total = (int64_t)fit.pad_left + fit.width + (fit.mark ? 1 : 0)
                            + fit.pad_right;
        CHECK(total == (c->width > 0 ? c->width : 0));
    }
}

/* R308: the fitter's width for text that fits is the width the canvas
 * spans for the same bytes. */
static void test_width_matches_canvas(void)
{
    CanvasScreen fixture;
    if (!canvas_screen_open(4, 1, &fixture)) {
        ++failures;
        return;
    }
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const Case *c = &cases[i];
        if (c->text == nullptr) {
            continue;
        }
        const OvdFit fit = ovd_textfit_measure(c->text, c->length, INT32_MAX, OVD_ALIGN_LEFT);
        const int32_t spanned = ovd_canvas_text(fixture.canvas, 0, 0, c->text, c->length,
                                                OVD_STYLE_DEFAULT);
        if (fit.length != c->length || fit.mark || fit.width != spanned) {
            fprintf(stderr, "width \"%s\": fitter %d (%zu bytes), canvas %d\n", c->name,
                    (int)fit.width, fit.length, (int)spanned);
            ++failures;
        }
    }
    canvas_screen_close(&fixture);
}

/* Drawing fills exactly width cells at (x, y), in the style, and nothing
 * else: the row is pre-filled with '#' to show untouched cells. */
static void draw_case(const char *text, size_t length, int32_t x, int32_t width, OvdAlign align,
                      const char *expected_text, const char *expected_styles)
{
    CanvasScreen fixture;
    if (!canvas_screen_open(8, 1, &fixture)) {
        ++failures;
        return;
    }
    const OvdStyle red = { .fg = ovd_color_indexed(1), .bg = OVD_COLOR_DEFAULT,
                           .attrs = OVD_ATTR_NONE };
    ovd_canvas_text(fixture.canvas, 0, 0, "########", 8, OVD_STYLE_DEFAULT);
    ovd_textfit_draw(fixture.canvas, x, 0, width, text, length, align, red);
    const char *const rows[] = { expected_text };
    const char *const styles[] = { expected_styles };
    const TestStyleKey legend[] = { { 'R', red } };
    if (!canvas_screen_expect(fixture.canvas, rows, styles, legend, 1)) {
        fprintf(stderr, "  draw of \"%.*s\" at x %d, width %d\n", (int)length, text, (int)x,
                (int)width);
        ++failures;
    }
    canvas_screen_close(&fixture);
}

static void test_draw(void)
{
    draw_case(TEXT("ab"), 1, 5, OVD_ALIGN_LEFT, "#ab   ##", ".RRRRR..");
    draw_case(TEXT("ab"), 1, 5, OVD_ALIGN_RIGHT, "#   ab##", ".RRRRR..");
    draw_case(TEXT("abcdef"), 2, 4, OVD_ALIGN_RIGHT, "##abc" ELLIPSIS "##", "..RRRR..");
    draw_case(TEXT("a" WIDE "b"), 0, 3, OVD_ALIGN_LEFT, "a" ELLIPSIS " #####", "RRR.....");
    draw_case(TEXT(WIDE "b"), 3, 1, OVD_ALIGN_LEFT, "### ####", "...R....");
    draw_case(TEXT(WIDE), 3, 2, OVD_ALIGN_LEFT, "###" WIDE "###", "...RR...");
    draw_case(TEXT(""), 2, 3, OVD_ALIGN_LEFT, "##   ###", "..RRR...");
    draw_case(TEXT("abc"), 2, 0, OVD_ALIGN_LEFT, "########", "........");
    draw_case(TEXT("abc"), 2, -3, OVD_ALIGN_LEFT, "########", "........");
    /* Past the right edge: only what lies on the canvas is drawn. */
    draw_case(TEXT("abcdef"), 6, 6, OVD_ALIGN_LEFT, "######ab", "......RR");
    /* Extreme coordinates neither overflow nor draw anything. */
    draw_case(TEXT("abc"), INT32_MAX - 1, INT32_MAX, OVD_ALIGN_RIGHT, "########", "........");
    draw_case(TEXT("abc"), INT32_MIN, INT32_MAX, OVD_ALIGN_RIGHT, "########", "........");
}

static bool same_rect(OvdRect a, OvdRect b)
{
    return a.x == b.x && a.y == b.y && a.cols == b.cols && a.rows == b.rows;
}

/* R309: the clip narrows to area within the current clip, and the clip it
 * replaced is restored. */
static void test_clip(void)
{
    CanvasScreen fixture;
    if (!canvas_screen_open(6, 2, &fixture)) {
        ++failures;
        return;
    }
    OvdCanvas *canvas = fixture.canvas;
    const OvdRect outer = { 1, 0, 4, 2 };
    ovd_canvas_set_clip(canvas, outer);

    OvdRect previous = ovd_widget_clip_enter(canvas, (OvdRect){ 3, -5, 10, 10 });
    CHECK(same_rect(previous, outer));
    CHECK(same_rect(ovd_canvas_clip(canvas), (OvdRect){ 3, 0, 2, 2 }));
    CHECK(ovd_canvas_text(canvas, 0, 0, "abcdef", 6, OVD_STYLE_DEFAULT) == 6);
    ovd_widget_clip_leave(canvas, previous);
    CHECK(same_rect(ovd_canvas_clip(canvas), outer));
    const char *const rows[] = { "   de ", "" };
    CHECK(canvas_screen_expect(canvas, rows, nullptr, nullptr, 0));

    /* Areas that leave nothing: no extent, disjoint, and at the int32_t
     * limits, where a 32-bit far edge would overflow. */
    const OvdRect nothing[] = {
        { 2, 0, 0, 2 },
        { 2, 0, 3, -1 },
        { 5, 0, 1, 2 },
        { INT32_MAX - 1, 0, INT32_MAX, 1 },
        { INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX },
    };
    for (size_t i = 0; i < sizeof nothing / sizeof nothing[0]; ++i) {
        previous = ovd_widget_clip_enter(canvas, nothing[i]);
        const OvdRect clip = ovd_canvas_clip(canvas);
        if (!same_rect(previous, outer) || clip.cols != 0 || clip.rows != 0) {
            fprintf(stderr, "clip_enter nothing[%zu]: got {%d, %d, %d, %d}\n", i, clip.x,
                    clip.y, clip.cols, clip.rows);
            ++failures;
        }
        ovd_widget_clip_leave(canvas, previous);
        CHECK(same_rect(ovd_canvas_clip(canvas), outer));
    }
    canvas_screen_close(&fixture);

    CHECK(same_rect(ovd_widget_clip_enter(nullptr, (OvdRect){ 0, 0, 1, 1 }),
                    (OvdRect){ 0, 0, 0, 0 }));
    ovd_widget_clip_leave(nullptr, (OvdRect){ 0, 0, 1, 1 });
}

static bool same_color(OvdColor a, OvdColor b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

/* R310: attrs OR'ed; only the overlay's non-default colors replace. */
static void test_combine(void)
{
    const OvdStyle red_bold = { .fg = ovd_color_indexed(1), .bg = ovd_color_rgb(1, 2, 3),
                                .attrs = OVD_ATTR_BOLD };
    const OvdStyle reverse = ovd_widget_style_default().selected;
    OvdStyle out = ovd_widget_style_combine(red_bold, reverse);
    CHECK(same_color(out.fg, red_bold.fg) && same_color(out.bg, red_bold.bg));
    CHECK(out.attrs == (OVD_ATTR_BOLD | OVD_ATTR_REVERSE));

    const OvdStyle blue_bg = { .fg = OVD_COLOR_DEFAULT, .bg = ovd_color_indexed(4),
                               .attrs = OVD_ATTR_UNDERLINE };
    out = ovd_widget_style_combine(red_bold, blue_bg);
    CHECK(same_color(out.fg, red_bold.fg) && same_color(out.bg, blue_bg.bg));
    CHECK(out.attrs == (OVD_ATTR_BOLD | OVD_ATTR_UNDERLINE));

    const OvdStyle green_fg = { .fg = ovd_color_rgb(0, 255, 0), .bg = OVD_COLOR_DEFAULT,
                                .attrs = OVD_ATTR_NONE };
    out = ovd_widget_style_combine(OVD_STYLE_DEFAULT, green_fg);
    CHECK(same_color(out.fg, green_fg.fg) && out.bg.kind == OVD_COLOR_KIND_DEFAULT);
    CHECK(out.attrs == OVD_ATTR_NONE);
}

int main(void)
{
    test_measure();
    test_width_matches_canvas();
    test_draw();
    test_clip();
    test_combine();
    if (failures != 0) {
        fprintf(stderr, "textfit: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("textfit: measuring, cutting, marking, padding, drawing, clip, and combine passed");
    return EXIT_SUCCESS;
}
