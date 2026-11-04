/* Tests for src/terminal.c (T031): the headless backend and the frame
 * state machine, through the public API only. FR-002, FR-012, FR-021,
 * FR-022, FR-025; research.md R21 to R25, R31, R35, R44. */
#include "support/test_alloc.h"

#include <overdrive/overdrive.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESET "\x1b[0m"
#define CLEAR "\x1b[2J"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

/* ---- Sink ------------------------------------------------------------ */

constexpr size_t CAPTURE_MAX = 65536;

/* What re-entrant calls made from inside the sink returned (R24). */
typedef struct Reentry {
    OvdResult begin;
    OvdResult end;
    OvdResult set_size;
    OvdSize size;
    OvdColorDepth depth;
    OvdSize canvas_size;
    int32_t canvas_text;
    bool canvas_cell;
} Reentry;

/* Records the bytes of the last sink call. */
typedef struct Capture {
    char bytes[CAPTURE_MAX];
    size_t length;
    size_t calls;
    bool fail;
    /* Re-entrancy probe: when terminal is set, the sink calls back into it. */
    OvdTerminal *terminal;
    OvdCanvas *canvas;
    Reentry reentry;
} Capture;

static bool capture_sink(void *context, const char *bytes, size_t length)
{
    Capture *capture = context;
    ++capture->calls;
    capture->length = 0;
    if (length <= sizeof capture->bytes) {
        memcpy(capture->bytes, bytes, length);
        capture->length = length;
    }
    if (capture->terminal != nullptr) {
        OvdTerminal *t = capture->terminal;
        OvdCanvas *dummy = nullptr;
        capture->reentry.begin = ovd_frame_begin(t, &dummy);
        capture->reentry.end = ovd_frame_end(t);
        capture->reentry.set_size = ovd_terminal_set_size(t, (OvdSize){ 3, 3 });
        ovd_frame_abandon(t);
        ovd_terminal_invalidate(t);
        capture->reentry.size = ovd_terminal_size(t);
        capture->reentry.depth = ovd_terminal_color_depth(t);
        capture->reentry.canvas_size = ovd_canvas_size(capture->canvas);
        capture->reentry.canvas_text
            = ovd_canvas_text(capture->canvas, 0, 0, "zz", 2, OVD_STYLE_DEFAULT);
        OvdCellView view;
        capture->reentry.canvas_cell = ovd_canvas_cell(capture->canvas, 0, 0, &view);
    }
    return !capture->fail;
}

static bool captured(const Capture *capture, const char *expected)
{
    const size_t length = strlen(expected);
    if (capture->length == length && memcmp(capture->bytes, expected, length) == 0) {
        return true;
    }
    fprintf(stderr, "  captured %zu bytes: \"", capture->length);
    for (size_t i = 0; i < capture->length; ++i) {
        const unsigned char c = (unsigned char)capture->bytes[i];
        if (c == 0x1B) {
            fputs("\\e", stderr);
        } else {
            fputc(c, stderr);
        }
    }
    fputs("\"\n", stderr);
    return false;
}

static bool starts_with(const Capture *capture, const char *prefix)
{
    const size_t length = strlen(prefix);
    return capture->length >= length && memcmp(capture->bytes, prefix, length) == 0;
}

/* ---- Fixture --------------------------------------------------------- */

typedef struct Fixture {
    TestAlloc alloc;
    OvdAllocator allocator;
    Capture capture;
    OvdTerminalConfig config;
    OvdTerminal *terminal;
} Fixture;

static void fixture_init(Fixture *f, int32_t cols, int32_t rows)
{
    memset(f, 0, sizeof *f);
    test_alloc_init(&f->alloc);
    f->allocator = test_alloc_allocator(&f->alloc);
    f->config = ovd_terminal_config_default();
    f->config.backend = OVD_BACKEND_HEADLESS;
    f->config.headless_size = (OvdSize){ cols, rows };
    f->config.sink = capture_sink;
    f->config.sink_context = &f->capture;
    f->config.allocator = &f->allocator;
}

static bool fixture_open(Fixture *f, int32_t cols, int32_t rows)
{
    fixture_init(f, cols, rows);
    return ovd_terminal_create(&f->config, &f->terminal) == OVD_OK && f->terminal != nullptr;
}

static bool fixture_done(Fixture *f)
{
    ovd_terminal_destroy(f->terminal);
    f->terminal = nullptr;
    const bool clean = f->alloc.live_blocks == 0 && f->alloc.violations == 0;
    if (!clean) {
        fprintf(stderr, "  live blocks %zu, violations %zu\n", f->alloc.live_blocks,
                f->alloc.violations);
    }
    test_alloc_release_all(&f->alloc);
    return clean;
}

static bool size_is(OvdSize size, int32_t cols, int32_t rows)
{
    return size.cols == cols && size.rows == rows;
}

/* One frame drawing text at (x, y); returns ovd_frame_end's result. */
static OvdResult frame_text(Fixture *f, int32_t x, int32_t y, const char *text)
{
    OvdCanvas *canvas = nullptr;
    const OvdResult begun = ovd_frame_begin(f->terminal, &canvas);
    if (begun != OVD_OK) {
        return begun;
    }
    if (text != nullptr) {
        ovd_canvas_text(canvas, x, y, text, strlen(text), OVD_STYLE_DEFAULT);
    }
    return ovd_frame_end(f->terminal);
}

/* ---- Configuration and creation -------------------------------------- */

static bool test_config_default(void)
{
    const OvdTerminalConfig c = ovd_terminal_config_default();
    CHECK(c.backend == OVD_BACKEND_SYSTEM);
    CHECK(c.detect_color_depth);
    CHECK(c.color_depth == OVD_COLOR_DEPTH_TRUECOLOR);
    CHECK(c.alternate_screen);
    CHECK(!c.synchronized_output);
    CHECK(size_is(c.headless_size, 80, 24));
    CHECK(c.sink == nullptr && c.sink_context == nullptr);
    CHECK(c.allocator == nullptr);
    return true;
}

static bool rejects(const OvdTerminalConfig *config, OvdResult expected, const TestAlloc *alloc)
{
    OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
    OvdTerminal *out = sentinel;
    const size_t calls = alloc->allocate_calls;
    CHECK(ovd_terminal_create(config, &out) == expected);
    CHECK(out == sentinel);
    CHECK(alloc->allocate_calls == calls);
    return true;
}

static bool test_create_validation(void)
{
    Fixture f;
    fixture_init(&f, 80, 24);
    OvdTerminal *out = nullptr;
    CHECK(ovd_terminal_create(nullptr, &out) == OVD_ERROR_INVALID_ARGUMENT && out == nullptr);
    CHECK(ovd_terminal_create(&f.config, nullptr) == OVD_ERROR_INVALID_ARGUMENT);

    OvdTerminalConfig c = f.config;
    c.backend = (OvdBackend)2;
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));
    c = f.config;
    c.color_depth = (OvdColorDepth)4;
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));
    c = f.config;
    c.headless_size = (OvdSize){ 0, 24 };
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));
    c.headless_size = (OvdSize){ 80, -1 };
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));
    c.headless_size = (OvdSize){ INT32_MIN, INT32_MIN };
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));

    OvdAllocator broken = f.allocator;
    broken.allocate = nullptr;
    c = f.config;
    c.allocator = &broken;
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));
    broken = f.allocator;
    broken.release = nullptr;
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));

    /* The system backend: the config is validated (color_depth even though
     * detection would override it), then declined until OVD-11 (R44). Its
     * headless_size is not validated. */
    c = f.config;
    c.backend = OVD_BACKEND_SYSTEM;
    c.color_depth = (OvdColorDepth)200;
    CHECK(rejects(&c, OVD_ERROR_INVALID_ARGUMENT, &f.alloc));
    c.color_depth = OVD_COLOR_DEPTH_256;
    c.headless_size = (OvdSize){ 0, 0 };
    CHECK(rejects(&c, OVD_ERROR_UNSUPPORTED, &f.alloc));

    /* A size whose buffers overflow is OVD_ERROR_ALLOCATION, with no
     * allocator call. */
    c = f.config;
    c.headless_size = (OvdSize){ INT32_MAX, INT32_MAX };
    CHECK(rejects(&c, OVD_ERROR_ALLOCATION, &f.alloc));

    CHECK(f.capture.calls == 0);
    CHECK(fixture_done(&f));
    return true;
}

static bool test_create_destroy(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 80, 24));
    CHECK(f.alloc.live_blocks > 0);
    CHECK(f.capture.calls == 0); /* headless writes nothing at create (R35) */
    CHECK(size_is(ovd_terminal_size(f.terminal), 80, 24));
    CHECK(ovd_terminal_color_depth(f.terminal) == OVD_COLOR_DEPTH_TRUECOLOR);
    ovd_terminal_destroy(f.terminal);
    f.terminal = nullptr;
    CHECK(f.capture.calls == 0); /* nor at destroy */
    CHECK(fixture_done(&f));

    /* The default allocator. */
    OvdTerminalConfig c = ovd_terminal_config_default();
    c.backend = OVD_BACKEND_HEADLESS;
    c.headless_size = (OvdSize){ 1, 1 };
    OvdTerminal *t = nullptr;
    CHECK(ovd_terminal_create(&c, &t) == OVD_OK && t != nullptr);
    CHECK(frame_text(&(Fixture){ .terminal = t }, 0, 0, "x") == OVD_OK);
    ovd_terminal_destroy(t);

    /* nullptr: infallible functions do nothing and return zero values (R22). */
    ovd_terminal_destroy(nullptr);
    ovd_terminal_invalidate(nullptr);
    ovd_frame_abandon(nullptr);
    CHECK(size_is(ovd_terminal_size(nullptr), 0, 0));
    CHECK(ovd_terminal_color_depth(nullptr) == OVD_COLOR_DEPTH_NONE);
    return true;
}

/* Every allocation at create may be refused: nothing is left behind. */
static bool test_create_allocation_failure(void)
{
    for (size_t refuse = 1; refuse <= 8; ++refuse) {
        Fixture f;
        fixture_init(&f, 10, 4);
        f.alloc.refuse_call = refuse;
        OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
        OvdTerminal *out = sentinel;
        const OvdResult result = ovd_terminal_create(&f.config, &out);
        if (refuse <= f.alloc.allocate_calls) {
            CHECK(result == OVD_ERROR_ALLOCATION);
            CHECK(out == sentinel);
            CHECK(f.alloc.live_blocks == 0 && f.alloc.violations == 0);
        } else {
            /* Fewer allocations than this: create succeeded. */
            CHECK(result == OVD_OK);
            f.terminal = out;
        }
        CHECK(fixture_done(&f));
    }
    return true;
}

/* R35: headless never reads the environment. CTest runs this test with
 * NO_COLOR=1 and COLORTERM=truecolor set. */
static bool test_headless_ignores_environment(void)
{
    const char *no_color = getenv("NO_COLOR");
    CHECK(no_color != nullptr && no_color[0] != '\0');
    Fixture f;
    fixture_init(&f, 4, 1);
    f.config.detect_color_depth = true;
    f.config.color_depth = OVD_COLOR_DEPTH_256;
    CHECK(ovd_terminal_create(&f.config, &f.terminal) == OVD_OK);
    CHECK(ovd_terminal_color_depth(f.terminal) == OVD_COLOR_DEPTH_256);
    CHECK(fixture_done(&f));
    return true;
}

/* ---- Frames ---------------------------------------------------------- */

static bool test_state_machine(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 6, 2));
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(nullptr, &canvas) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_frame_begin(f.terminal, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_frame_end(nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_frame_end(f.terminal) == OVD_ERROR_STATE);
    CHECK(canvas == nullptr);

    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK && canvas != nullptr);
    CHECK(size_is(ovd_canvas_size(canvas), 6, 2));
    CHECK(ovd_canvas_text(canvas, 0, 0, "ab", 2, OVD_STYLE_DEFAULT) == 2);

    /* begin while open fails and leaves the frame open (R21). */
    OvdCanvas *again = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &again) == OVD_ERROR_STATE && again == nullptr);
    OvdCellView view;
    CHECK(ovd_canvas_cell(canvas, 0, 0, &view) && strcmp(view.text, "a") == 0);

    /* abandon closes without writing; again does nothing. */
    ovd_frame_abandon(f.terminal);
    ovd_frame_abandon(f.terminal);
    CHECK(f.capture.calls == 0);
    CHECK(size_is(ovd_canvas_size(canvas), 0, 0)); /* closed (R23) */
    CHECK(ovd_frame_end(f.terminal) == OVD_ERROR_STATE);

    /* The same canvas every frame, cleared and fully clipped at begin. */
    OvdCanvas *next = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &next) == OVD_OK && next == canvas);
    CHECK(ovd_canvas_cell(canvas, 0, 0, &view) && strcmp(view.text, " ") == 0);
    const OvdRect clip = ovd_canvas_clip(canvas);
    CHECK(clip.x == 0 && clip.y == 0 && clip.cols == 6 && clip.rows == 2);
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    CHECK(size_is(ovd_canvas_size(canvas), 0, 0));
    CHECK(fixture_done(&f));
    return true;
}

static bool test_frame_output(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 6, 2));
    /* The first frame is a full redraw (FR-025), in one sink call. */
    CHECK(frame_text(&f, 0, 0, "hi") == OVD_OK);
    CHECK(f.capture.calls == 1);
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[1;1Hhi" RESET));

    /* An identical frame writes nothing: no sink call (FR-021). */
    CHECK(frame_text(&f, 0, 0, "hi") == OVD_OK);
    CHECK(f.capture.calls == 1);

    /* Only the difference. */
    CHECK(frame_text(&f, 0, 0, "ho") == OVD_OK);
    CHECK(f.capture.calls == 2);
    CHECK(captured(&f.capture, "\x1b[1;2Ho" RESET));

    /* A blank frame erases what was drawn. */
    CHECK(frame_text(&f, 0, 0, nullptr) == OVD_OK);
    CHECK(f.capture.calls == 3);
    CHECK(captured(&f.capture, "\x1b[1;1H  " RESET));

    /* Frames that do not resize never allocate. */
    const size_t calls = f.alloc.allocate_calls;
    for (int i = 0; i < 20; ++i) {
        CHECK(frame_text(&f, i % 5, i % 2, "x") == OVD_OK);
    }
    CHECK(f.alloc.allocate_calls == calls && f.alloc.release_calls == 0);
    CHECK(fixture_done(&f));
    return true;
}

static bool test_synchronized_output(void)
{
    Fixture f;
    fixture_init(&f, 4, 1);
    f.config.synchronized_output = true;
    CHECK(ovd_terminal_create(&f.config, &f.terminal) == OVD_OK);
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    CHECK(captured(&f.capture, "\x1b[?2026h" RESET CLEAR "\x1b[1;1Ha" RESET "\x1b[?2026l"));
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    CHECK(f.capture.calls == 1);
    CHECK(frame_text(&f, 1, 0, "b") == OVD_OK);
    CHECK(captured(&f.capture, "\x1b[?2026h\x1b[1;1H b" RESET "\x1b[?2026l"));
    CHECK(fixture_done(&f));
    return true;
}

static bool test_color_depth_applied(void)
{
    Fixture f;
    fixture_init(&f, 4, 1);
    f.config.color_depth = OVD_COLOR_DEPTH_NONE;
    CHECK(ovd_terminal_create(&f.config, &f.terminal) == OVD_OK);
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    const OvdStyle red = { .fg = ovd_color_rgb(255, 0, 0), .attrs = OVD_ATTR_BOLD };
    CHECK(ovd_canvas_text(canvas, 0, 0, "r", 1, red) == 1);
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    /* Depth NONE: attributes only, no color parameters. */
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[1;1H\x1b[1mr" RESET));
    CHECK(fixture_done(&f));
    return true;
}

static bool test_invalidate(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 1));
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    ovd_terminal_invalidate(f.terminal);
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    CHECK(f.capture.calls == 2);
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[1;1Ha" RESET));

    /* Invalidating an unknown screen, or while a frame is open, works too. */
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    ovd_canvas_text(canvas, 0, 0, "a", 1, OVD_STYLE_DEFAULT);
    ovd_terminal_invalidate(f.terminal);
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    CHECK(f.capture.calls == 3 && starts_with(&f.capture, RESET CLEAR));
    CHECK(fixture_done(&f));
    return true;
}

/* A failed sink: OVD_ERROR_IO, the frame is closed, and the next frame is
 * a full redraw (R21's one exception). */
static bool test_sink_failure(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 1));
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    f.capture.fail = true;
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    ovd_canvas_text(canvas, 0, 0, "b", 1, OVD_STYLE_DEFAULT);
    CHECK(ovd_frame_end(f.terminal) == OVD_ERROR_IO);
    CHECK(f.capture.calls == 2);
    CHECK(ovd_frame_end(f.terminal) == OVD_ERROR_STATE);
    CHECK(size_is(ovd_canvas_size(canvas), 0, 0));

    f.capture.fail = false;
    CHECK(frame_text(&f, 0, 0, "b") == OVD_OK);
    CHECK(f.capture.calls == 3);
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[1;1Hb" RESET));
    CHECK(fixture_done(&f));

    /* No sink: output is discarded, and counts as delivered. */
    fixture_init(&f, 4, 1);
    f.config.sink = nullptr;
    CHECK(ovd_terminal_create(&f.config, &f.terminal) == OVD_OK);
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    CHECK(fixture_done(&f));
    return true;
}

static bool test_destroy_open_frame(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 2));
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    ovd_canvas_text(canvas, 0, 0, "abc", 3, OVD_STYLE_DEFAULT);
    CHECK(fixture_done(&f)); /* destroy abandons it: nothing written, all freed */
    CHECK(f.capture.calls == 0);
    return true;
}

/* ---- Size ------------------------------------------------------------ */

static bool test_set_size(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 2));
    CHECK(ovd_terminal_set_size(nullptr, (OvdSize){ 3, 3 }) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 0, 3 }) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 3, -3 }) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(size_is(ovd_terminal_size(f.terminal), 4, 2));
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);

    /* While a frame is open: OVD_ERROR_STATE, and nothing pends. */
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 7, 3 }) == OVD_ERROR_STATE);
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 0, 3 }) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    CHECK(size_is(ovd_terminal_size(f.terminal), 4, 2));

    /* A pending size is reported at once and applied by the next begin,
     * which allocates new buffers before releasing the old (R31) and
     * makes the frame a full redraw. The last set wins. */
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 9, 9 }) == OVD_OK);
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 7, 3 }) == OVD_OK);
    CHECK(size_is(ovd_terminal_size(f.terminal), 7, 3));
    const size_t live = f.alloc.live_blocks;
    const size_t allocated = f.alloc.allocate_calls;
    const size_t released = f.alloc.release_calls;
    f.alloc.event_count = 0;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    CHECK(size_is(ovd_canvas_size(canvas), 7, 3));
    const size_t taken = f.alloc.allocate_calls - allocated;
    CHECK(taken > 0 && f.alloc.release_calls - released == taken);
    CHECK(f.alloc.live_blocks == live);
    for (size_t i = 0; i < taken; ++i) {
        CHECK(f.alloc.events[i].kind == TEST_ALLOC_EVENT_ALLOCATE);
    }
    CHECK(ovd_canvas_text(canvas, 6, 2, "z", 1, OVD_STYLE_DEFAULT) == 1);
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[3;7Hz" RESET));
    CHECK(size_is(ovd_terminal_size(f.terminal), 7, 3));

    /* Setting the current size changes nothing: no allocation, no redraw. */
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 7, 3 }) == OVD_OK);
    const size_t before = f.alloc.allocate_calls;
    const size_t calls = f.capture.calls;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    CHECK(ovd_canvas_text(canvas, 6, 2, "z", 1, OVD_STYLE_DEFAULT) == 1);
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    CHECK(f.alloc.allocate_calls == before && f.capture.calls == calls);

    /* A 1x1 terminal works (R25). */
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 1, 1 }) == OVD_OK);
    CHECK(frame_text(&f, 0, 0, "q") == OVD_OK);
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[1;1Hq" RESET));
    CHECK(fixture_done(&f));
    return true;
}

/* R21, R31: a refused resize changes nothing; the pending size is kept,
 * and the next begin applies it. */
static bool test_resize_allocation_failure(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 4, 2));
    CHECK(frame_text(&f, 0, 0, "a") == OVD_OK);
    const size_t created = f.alloc.allocate_calls;
    for (size_t refuse = 1; refuse <= 3; ++refuse) {
        CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 8, 3 }) == OVD_OK);
        const size_t live = f.alloc.live_blocks;
        f.alloc.refuse_call = f.alloc.allocate_calls + refuse;
        OvdCanvas *sentinel = (OvdCanvas *)&sentinel;
        OvdCanvas *canvas = sentinel;
        CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_ERROR_ALLOCATION);
        CHECK(canvas == sentinel);
        CHECK(f.alloc.live_blocks == live && f.alloc.violations == 0);
        CHECK(size_is(ovd_terminal_size(f.terminal), 8, 3)); /* still pending */
        CHECK(ovd_frame_end(f.terminal) == OVD_ERROR_STATE); /* no frame opened */
        f.alloc.refuse_call = 0;
    }
    CHECK(f.alloc.allocate_calls > created);

    /* The next begin applies it; the old screen was kept, but the new size
     * is a full redraw. */
    CHECK(frame_text(&f, 7, 2, "w") == OVD_OK);
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[3;8Hw" RESET));
    CHECK(size_is(ovd_terminal_size(f.terminal), 8, 3));
    CHECK(fixture_done(&f));

    /* A refused resize while the old size never had a frame. */
    CHECK(fixture_open(&f, 4, 2));
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 5, 5 }) == OVD_OK);
    f.alloc.refuse_all = true;
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_ERROR_ALLOCATION);
    f.alloc.refuse_all = false;
    CHECK(frame_text(&f, 0, 0, "k") == OVD_OK);
    CHECK(captured(&f.capture, RESET CLEAR "\x1b[1;1Hk" RESET));
    CHECK(fixture_done(&f));
    return true;
}

/* ---- Isolation and re-entrancy --------------------------------------- */

/* FR-002: two terminals at once, independent. */
static bool test_two_terminals(void)
{
    Fixture a;
    Fixture b;
    CHECK(fixture_open(&a, 4, 1));
    CHECK(fixture_open(&b, 3, 2));
    OvdCanvas *ca = nullptr;
    OvdCanvas *cb = nullptr;
    CHECK(ovd_frame_begin(a.terminal, &ca) == OVD_OK);
    CHECK(ovd_frame_begin(b.terminal, &cb) == OVD_OK);
    CHECK(ca != cb);
    ovd_canvas_text(ca, 0, 0, "aa", 2, OVD_STYLE_DEFAULT);
    ovd_canvas_text(cb, 0, 1, "b", 1, OVD_STYLE_DEFAULT);
    CHECK(ovd_frame_end(b.terminal) == OVD_OK);
    CHECK(a.capture.calls == 0);
    CHECK(captured(&b.capture, RESET CLEAR "\x1b[2;1Hb" RESET));
    CHECK(ovd_frame_end(a.terminal) == OVD_OK);
    CHECK(captured(&a.capture, RESET CLEAR "\x1b[1;1Haa" RESET));
    ovd_terminal_invalidate(a.terminal);
    CHECK(frame_text(&b, 0, 1, "b") == OVD_OK);
    CHECK(b.capture.calls == 1);
    CHECK(fixture_done(&a));
    CHECK(fixture_done(&b));
    return true;
}

/* R24: while the sink runs, the terminal is busy and its canvas closed. */
static bool test_sink_reentrancy(void)
{
    Fixture f;
    CHECK(fixture_open(&f, 5, 2));
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    ovd_canvas_text(canvas, 0, 0, "abc", 3, OVD_STYLE_DEFAULT);
    f.capture.terminal = f.terminal;
    f.capture.canvas = canvas;
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    f.capture.terminal = nullptr;
    CHECK(f.capture.calls == 1);

    const Reentry *r = &f.capture.reentry;
    CHECK(r->begin == OVD_ERROR_STATE);
    CHECK(r->end == OVD_ERROR_STATE);
    CHECK(r->set_size == OVD_ERROR_STATE);
    CHECK(size_is(r->size, 5, 2));
    CHECK(r->depth == OVD_COLOR_DEPTH_TRUECOLOR);
    CHECK(size_is(r->canvas_size, 0, 0));
    CHECK(r->canvas_text == 0);
    CHECK(!r->canvas_cell);

    /* invalidate and abandon in the sink did nothing: the same frame again
     * writes nothing, and the size did not change. */
    CHECK(frame_text(&f, 0, 0, "abc") == OVD_OK);
    CHECK(f.capture.calls == 1);
    CHECK(size_is(ovd_terminal_size(f.terminal), 5, 2));
    CHECK(fixture_done(&f));
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "config_default", test_config_default },
        { "create_validation", test_create_validation },
        { "create_destroy", test_create_destroy },
        { "create_allocation_failure", test_create_allocation_failure },
        { "headless_ignores_environment", test_headless_ignores_environment },
        { "state_machine", test_state_machine },
        { "frame_output", test_frame_output },
        { "synchronized_output", test_synchronized_output },
        { "color_depth_applied", test_color_depth_applied },
        { "invalidate", test_invalidate },
        { "sink_failure", test_sink_failure },
        { "destroy_open_frame", test_destroy_open_frame },
        { "set_size", test_set_size },
        { "resize_allocation_failure", test_resize_allocation_failure },
        { "two_terminals", test_two_terminals },
        { "sink_reentrancy", test_sink_reentrancy },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "terminal: %s failed\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        fprintf(stderr, "terminal: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("terminal: headless backend and frame state machine passed");
    return EXIT_SUCCESS;
}
