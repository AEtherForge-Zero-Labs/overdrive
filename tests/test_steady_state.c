/* Steady-state allocation test (T035, SC-004): frames whose size equals
 * the terminal's current buffers make 0 allocator calls, whatever they
 * draw. Public API only, with the counting allocator. Each case checks
 * the first frame after create and the 100 after it, then a resize, then
 * the 100 frames after the resizing frame. research.md R30 to R32, R47. */
#include "support/test_alloc.h"

#include <overdrive/overdrive.h>

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

constexpr int32_t STEADY_FRAMES = 100;

/* ---- Sink ------------------------------------------------------------ */

/* Counts what reaches the sink; fails the call when fail is set. */
typedef struct Sink {
    size_t calls;
    size_t bytes;
    size_t full_redraws; /* calls whose output erases the display */
    bool fail;
} Sink;

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

static bool sink(void *context, const char *bytes, size_t length)
{
    Sink *s = context;
    ++s->calls;
    s->bytes += length;
    if (contains(bytes, length, "\x1b[2J")) {
        ++s->full_redraws;
    }
    return !s->fail;
}

/* ---- Random content -------------------------------------------------- */

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* A coordinate or extent: the int32_t extremes, small negatives, zero,
 * or a value in [-1, limit]. */
static int32_t random_int(uint32_t *state, int32_t limit)
{
    switch (next_random(state) % 8) {
    case 0:
        return INT32_MIN;
    case 1:
        return INT32_MAX;
    case 2:
        return -1;
    case 3:
        return 0;
    default:
        return (int32_t)(next_random(state) % ((uint32_t)limit + 2)) - 1;
    }
}

static OvdRect random_rect(uint32_t *state, OvdSize size)
{
    return (OvdRect){
        .x = random_int(state, size.cols),
        .y = random_int(state, size.rows),
        .cols = random_int(state, size.cols),
        .rows = random_int(state, size.rows),
    };
}

/* Default, indexed, and RGB colors, plus an out-of-range kind. */
static OvdColor random_color(uint32_t *state)
{
    const uint32_t bits = next_random(state);
    switch (bits % 4) {
    case 0:
        return OVD_COLOR_DEFAULT;
    case 1:
        return ovd_color_indexed((uint8_t)(bits >> 8));
    case 2:
        return ovd_color_hex(bits >> 8);
    default:
        return (OvdColor){ .kind = (OvdColorKind)(3 + bits % 200), .index = (uint8_t)bits };
    }
}

/* Every attribute bit, defined or not. */
static OvdStyle random_style(uint32_t *state)
{
    return (OvdStyle){
        .fg = random_color(state),
        .bg = random_color(state),
        .attrs = (uint16_t)next_random(state),
    };
}

#define TEXT(literal) { literal, sizeof literal - 1 }

/* Untrusted text: controls, escape sequences, ill-formed UTF-8, wide and
 * long graphemes (FR-031, FR-032). */
static const struct {
    const char *text;
    size_t length;
} hostile[] = {
    TEXT("plain text"),
    TEXT("\x1b[2J\x1b[31mred\x1b[0m"),
    TEXT("\x1b]0;title\x07"),
    TEXT("\xc2\x9b" "31m"),
    TEXT("tab\there\nnewline\r"),
    TEXT("nul\0inside"),
    TEXT("\x7f" "del"),
    TEXT("\xf0\x80\x80" "\xf0\x9f\x98" "\xff\xfe"),
    TEXT("\xe2\x80\xa8" "\xe2\x80\xa9"),
    TEXT("\xe2\x80\xae" "bidi"),
    TEXT("\xe4\xb8\xad\xe6\x96\x87"),
    TEXT("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7"),
    TEXT("e\xcc\x81" "\xcc\x81"),
    TEXT("a\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81"),
    TEXT("\xcc\x81" "lone mark"),
    TEXT("\xf0\x9d\x90\x80"),
};

/* Draws a random mix of operations from seed. The same seed on a canvas
 * of the same size draws the same frame. */
static void draw_random(OvdCanvas *canvas, uint32_t seed)
{
    uint32_t state = seed;
    const OvdSize size = ovd_canvas_size(canvas);
    const uint32_t operations = 1 + next_random(&state) % 40;
    for (uint32_t i = 0; i < operations; ++i) {
        switch (next_random(&state) % 6) {
        case 0:
            ovd_canvas_fill(canvas, random_rect(&state, size), random_style(&state));
            break;
        case 1: {
            /* Includes one out-of-range border value. */
            const OvdBorder border = (OvdBorder)(next_random(&state) % 6);
            ovd_canvas_box(canvas, random_rect(&state, size), border, random_style(&state));
            break;
        }
        case 2:
            ovd_canvas_set_clip(canvas, random_rect(&state, size));
            break;
        case 3: {
            OvdCellView view;
            ovd_canvas_cell(canvas, random_int(&state, size.cols), random_int(&state, size.rows),
                            &view);
            break;
        }
        default: {
            const size_t pick = next_random(&state) % (sizeof hostile / sizeof hostile[0]);
            ovd_canvas_text(canvas, random_int(&state, size.cols), random_int(&state, size.rows),
                            hostile[pick].text, hostile[pick].length, random_style(&state));
            break;
        }
        }
    }
}

/* ---- One case -------------------------------------------------------- */

typedef struct Run {
    TestAlloc alloc;
    OvdAllocator allocator;
    Sink sink;
    OvdTerminal *terminal;
    uint32_t random;
    uint32_t previous_seed;
    size_t quiet_frames; /* ended with OVD_OK and wrote nothing */
} Run;

/* Frame n. Periodic events put the terminal through every state a frame
 * can pass through; none of them may allocate. */
static bool steady_frame(Run *r, int32_t n)
{
    OvdTerminal *t = r->terminal;
    if (n % 7 == 0) {
        ovd_terminal_invalidate(t); /* a full redraw */
    }
    if (n % 17 == 0) {
        /* A pending size equal to the current one (R21). */
        CHECK(ovd_terminal_set_size(t, ovd_terminal_size(t)) == OVD_OK);
    }
    CHECK(ovd_frame_end(t) == OVD_ERROR_STATE);

    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(t, &canvas) == OVD_OK && canvas != nullptr);
    OvdCanvas *again = nullptr;
    CHECK(ovd_frame_begin(t, &again) == OVD_ERROR_STATE);
    /* Every fifth frame repeats the previous one: unchanged, 0 bytes,
     * unless that frame did not reach the screen. */
    const uint32_t seed = n % 5 == 0 ? r->previous_seed : next_random(&r->random);
    r->previous_seed = seed;
    draw_random(canvas, seed);

    if (n % 13 == 0) {
        ovd_frame_abandon(t);
        return true;
    }
    const bool fail = n % 11 == 0;
    r->sink.fail = fail;
    const size_t calls = r->sink.calls;
    const OvdResult ended = ovd_frame_end(t);
    r->sink.fail = false;
    const bool wrote = r->sink.calls > calls;
    CHECK(ended == (fail && wrote ? OVD_ERROR_IO : OVD_OK));
    if (ended == OVD_OK && !wrote) {
        ++r->quiet_frames;
    }
    /* The canvas is closed between frames: drawing on it does nothing. */
    CHECK(ovd_canvas_text(canvas, 0, 0, "x", 1, OVD_STYLE_DEFAULT) == 0);
    return true;
}

/* Frames start to start + count - 1, each checked for allocator calls. */
static bool steady_frames(Run *r, int32_t start, int32_t count)
{
    const size_t allocations = r->alloc.allocate_calls;
    const size_t releases = r->alloc.release_calls;
    const size_t live = r->alloc.live_blocks;
    for (int32_t n = start; n < start + count; ++n) {
        if (!steady_frame(r, n)) {
            fprintf(stderr, "  frame %d failed\n", n);
            return false;
        }
        if (r->alloc.allocate_calls != allocations || r->alloc.release_calls != releases) {
            fprintf(stderr, "  frame %d: %zu allocate and %zu release calls\n", n,
                    r->alloc.allocate_calls - allocations, r->alloc.release_calls - releases);
            return false;
        }
    }
    CHECK(r->alloc.live_blocks == live && r->alloc.violations == 0);
    return true;
}

static bool phases(Run *r, OvdSize second)
{
    /* The size is the one create allocated for: the first frame and the
     * 100 after it make no allocator call. */
    CHECK(steady_frames(r, 1, 1 + STEADY_FRAMES));

    /* A resize allocates (R31). Seeing it shows the counter is live. */
    const size_t allocations = r->alloc.allocate_calls;
    const size_t releases = r->alloc.release_calls;
    CHECK(ovd_terminal_set_size(r->terminal, second) == OVD_OK);
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(r->terminal, &canvas) == OVD_OK);
    CHECK(r->alloc.allocate_calls > allocations && r->alloc.release_calls > releases);
    const OvdSize size = ovd_canvas_size(canvas);
    CHECK(size.cols == second.cols && size.rows == second.rows);
    draw_random(canvas, next_random(&r->random));
    CHECK(ovd_frame_end(r->terminal) == OVD_OK);

    /* The 100 frames after the resizing frame make none either. */
    CHECK(steady_frames(r, 3 + STEADY_FRAMES, STEADY_FRAMES));

    /* The frames did reach the sink: full redraws, other output, and
     * unchanged frames that wrote nothing. */
    CHECK(r->sink.full_redraws > 0 && r->sink.bytes > 0 && r->quiet_frames > 0);
    return true;
}

static bool run_case(OvdSize first, OvdSize second, OvdColorDepth depth, bool synchronized,
                     uint32_t seed)
{
    Run r = { .random = seed == 0 ? 1 : seed };
    test_alloc_init(&r.alloc);
    r.allocator = test_alloc_allocator(&r.alloc);
    OvdTerminalConfig config = ovd_terminal_config_default();
    config.backend = OVD_BACKEND_HEADLESS;
    config.headless_size = first;
    config.color_depth = depth;
    config.synchronized_output = synchronized;
    config.sink = sink;
    config.sink_context = &r.sink;
    config.allocator = &r.allocator;

    bool ok = ovd_terminal_create(&config, &r.terminal) == OVD_OK;
    if (ok) {
        ok = phases(&r, second);
        ovd_terminal_destroy(r.terminal);
    } else {
        fprintf(stderr, "  create failed\n");
    }
    const bool clean = r.alloc.live_blocks == 0 && r.alloc.violations == 0;
    if (!ok || !clean) {
        fprintf(stderr, "  case %dx%d -> %dx%d, depth %d, synchronized %d: live %zu, violations %zu\n",
                first.cols, first.rows, second.cols, second.rows, (int)depth, (int)synchronized,
                r.alloc.live_blocks, r.alloc.violations);
    }
    test_alloc_release_all(&r.alloc);
    return ok && clean;
}

int main(void)
{
    static const struct {
        OvdSize first;
        OvdSize second;
    } pairs[] = {
        { { 1, 1 }, { 200, 60 } },
        { { 80, 24 }, { 1, 1 } },
        { { 200, 60 }, { 80, 24 } },
    };
    static const OvdColorDepth depths[] = {
        OVD_COLOR_DEPTH_NONE,
        OVD_COLOR_DEPTH_16,
        OVD_COLOR_DEPTH_256,
        OVD_COLOR_DEPTH_TRUECOLOR,
    };
    int failures = 0;
    int cases = 0;
    uint32_t seed = 0x2545F491u;
    for (size_t p = 0; p < sizeof pairs / sizeof pairs[0]; ++p) {
        for (size_t d = 0; d < sizeof depths / sizeof depths[0]; ++d) {
            for (int sync = 0; sync < 2; ++sync) {
                seed += 0x9E3779B9u;
                ++cases;
                if (!run_case(pairs[p].first, pairs[p].second, depths[d], sync != 0, seed)) {
                    ++failures;
                }
            }
        }
    }
    if (failures != 0) {
        fprintf(stderr, "steady_state: %d of %d case(s) failed\n", failures, cases);
        return EXIT_FAILURE;
    }
    printf("steady_state: %d cases, 0 allocator calls in each case's %d frames at create's "
           "size and %d after a resize\n",
           cases, 1 + STEADY_FRAMES, STEADY_FRAMES);
    return EXIT_SUCCESS;
}
