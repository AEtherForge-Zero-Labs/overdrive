/* Tests for the shared widget pieces (004 T305), public API only: the
 * default appearance (R311), the array item helper (SC-001), and a
 * self-check of the canvas read-back the widget tests rely on (R319). */
#include "support/canvas_screen.h"

#include <overdrive/overdrive.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDE "\xE4\xB8\xAD" /* U+4E2D, East_Asian_Width W */

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

static bool default_colors(OvdStyle style)
{
    return style.fg.kind == OVD_COLOR_KIND_DEFAULT && style.bg.kind == OVD_COLOR_KIND_DEFAULT;
}

/* R311: readable on a monochrome terminal. */
static void test_style_default(void)
{
    const OvdWidgetStyle style = ovd_widget_style_default();
    CHECK(default_colors(style.normal) && style.normal.attrs == OVD_ATTR_NONE);
    CHECK(default_colors(style.selected) && style.selected.attrs == OVD_ATTR_REVERSE);
    CHECK(default_colors(style.cursor) && style.cursor.attrs == OVD_ATTR_REVERSE);
    CHECK(default_colors(style.header) && style.header.attrs == OVD_ATTR_BOLD);
    CHECK(default_colors(style.empty) && style.empty.attrs == OVD_ATTR_DIM);
}

static bool text_is(OvdText text, const char *expected)
{
    return text.bytes != nullptr && text.length == strlen(expected)
        && memcmp(text.bytes, expected, text.length) == 0;
}

static bool text_empty(OvdText text)
{
    return text.length == 0;
}

static void test_array_item(void)
{
    static const char *const entries[] = { "Open", "Save", WIDE "x" };
    void *context = (void *)entries;
    CHECK(text_is(ovd_text_array_item(context, 0), "Open"));
    CHECK(text_is(ovd_text_array_item(context, 1), "Save"));
    CHECK(text_is(ovd_text_array_item(context, 2), WIDE "x"));
    CHECK(text_empty(ovd_text_array_item(nullptr, 0)));
    CHECK(text_empty(ovd_text_array_item(context, -1)));
    CHECK(text_empty(ovd_text_array_item(context, INT32_MIN)));
    /* A nullptr entry in the array is empty text, as ovd_text gives. */
    static const char *const holes[] = { nullptr };
    CHECK(text_empty(ovd_text_array_item((void *)holes, 0)));
}

/* R319's helper accepts a matching screen and rejects a wrong one, with
 * a message naming the cell (printed to stderr). */
static void test_canvas_screen(void)
{
    CanvasScreen fixture;
    if (!canvas_screen_open(6, 2, &fixture)) {
        ++failures;
        return;
    }
    const OvdStyle red = { .fg = ovd_color_indexed(1), .bg = OVD_COLOR_DEFAULT,
                           .attrs = OVD_ATTR_NONE };
    CHECK(ovd_canvas_text(fixture.canvas, 1, 1, "ab" WIDE, 5, red) == 4);
    const TestStyleKey legend[] = { { 'R', red } };

    const char *const rows[] = { "", " ab" WIDE " " };
    const char *const styles[] = { "", ".RRRR." };
    CHECK(canvas_screen_expect(fixture.canvas, rows, styles, legend, 1));

    fputs("test_widget: the next mismatch is expected (self-check):\n", stderr);
    const char *const wrong_rows[] = { "", " ac" WIDE " " };
    CHECK(!canvas_screen_expect(fixture.canvas, wrong_rows, styles, legend, 1));
    const char *const wrong_styles[] = { "", ".RR..." };
    fputs("test_widget: the next mismatch is expected (self-check):\n", stderr);
    CHECK(!canvas_screen_expect(fixture.canvas, rows, wrong_styles, legend, 1));

    /* The capture itself: a wide lead, then its skipped continuation. */
    TestScreen *screen = canvas_screen_capture(fixture.canvas);
    CHECK(screen != nullptr);
    if (screen != nullptr) {
        const TestCell *lead = test_screen_cell(screen, 3, 1);
        const TestCell *continuation = test_screen_cell(screen, 4, 1);
        CHECK(lead != nullptr && lead->width == 2 && strcmp(lead->text, WIDE) == 0);
        CHECK(continuation != nullptr && continuation->width == 0);
        test_screen_destroy(screen);
    }
    canvas_screen_close(&fixture);
    CHECK(canvas_screen_capture(nullptr) == nullptr);
}

int main(void)
{
    test_style_default();
    test_array_item();
    test_canvas_screen();
    if (failures != 0) {
        fprintf(stderr, "widget: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("widget: default style, array items, and canvas read-back passed");
    return EXIT_SUCCESS;
}
