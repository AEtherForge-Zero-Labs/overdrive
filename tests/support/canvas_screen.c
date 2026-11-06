/* Canvas read-back for widget tests: canvas_screen.h. */
#include "canvas_screen.h"

#include <stdio.h>
#include <string.h>

bool canvas_screen_open(int32_t cols, int32_t rows, CanvasScreen *fixture)
{
    *fixture = (CanvasScreen){ 0 };
    OvdTerminalConfig config = ovd_terminal_config_default();
    config.backend = OVD_BACKEND_HEADLESS;
    config.headless_size = (OvdSize){ cols, rows };
    config.sink = nullptr; /* discard output */
    OvdTerminal *terminal = nullptr;
    if (ovd_terminal_create(&config, &terminal) != OVD_OK) {
        fprintf(stderr, "canvas_screen: ovd_terminal_create failed for %dx%d\n", (int)cols,
                (int)rows);
        return false;
    }
    OvdCanvas *canvas = nullptr;
    if (ovd_frame_begin(terminal, &canvas) != OVD_OK) {
        fprintf(stderr, "canvas_screen: ovd_frame_begin failed\n");
        ovd_terminal_destroy(terminal);
        return false;
    }
    *fixture = (CanvasScreen){ .terminal = terminal, .canvas = canvas };
    return true;
}

void canvas_screen_close(CanvasScreen *fixture)
{
    if (fixture->terminal != nullptr) {
        ovd_frame_abandon(fixture->terminal);
        ovd_terminal_destroy(fixture->terminal);
    }
    *fixture = (CanvasScreen){ 0 };
}

TestScreen *canvas_screen_capture(const OvdCanvas *canvas)
{
    const OvdSize size = ovd_canvas_size(canvas);
    if (size.cols <= 0 || size.rows <= 0) {
        return nullptr;
    }
    TestScreen *screen = test_screen_create(size.cols, size.rows);
    if (screen == nullptr) {
        return nullptr;
    }
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            OvdCellView cell;
            if (!ovd_canvas_cell(canvas, x, y, &cell)) {
                test_screen_destroy(screen);
                return nullptr;
            }
            if (cell.width == 0) {
                continue; /* the continuation of the wide lead before it */
            }
            test_screen_put(screen, x, y, cell.text, cell.length, cell.width, cell.style);
        }
    }
    return screen;
}

bool canvas_screen_expect(const OvdCanvas *canvas, const char *const text_rows[],
                          const char *const style_rows[], const TestStyleKey legend[],
                          size_t legend_count)
{
    const OvdSize size = ovd_canvas_size(canvas);
    char message[512];
    TestScreen *expected = test_screen_from_rows(size.cols, size.rows, canvas_screen_classify,
                                                 text_rows, style_rows, legend, legend_count,
                                                 message, sizeof message);
    if (expected == nullptr) {
        fprintf(stderr, "canvas_screen: bad expectation: %s\n", message);
        return false;
    }
    TestScreen *actual = canvas_screen_capture(canvas);
    bool equal = false;
    if (actual == nullptr) {
        fprintf(stderr, "canvas_screen: could not read the canvas back\n");
    } else {
        equal = test_screen_equal(expected, actual, message, sizeof message);
        if (!equal) {
            char dump[4096];
            test_screen_dump(expected, dump, sizeof dump);
            fprintf(stderr, "canvas_screen: mismatch: %s\nexpected:\n%s", message, dump);
            test_screen_dump(actual, dump, sizeof dump);
            fprintf(stderr, "actual:\n%s", dump);
        }
    }
    test_screen_destroy(actual);
    test_screen_destroy(expected);
    return equal;
}

TestWidth canvas_screen_classify(uint32_t code_point)
{
    if (code_point >= 0x4E00 && code_point <= 0x9FFF) {
        return TEST_WIDTH_WIDE;
    }
    if (code_point >= 0x0300 && code_point <= 0x036F) {
        return TEST_WIDTH_EXTEND;
    }
    return TEST_WIDTH_NARROW;
}
