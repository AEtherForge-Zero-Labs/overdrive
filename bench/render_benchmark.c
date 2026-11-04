/* Render benchmark (T036, US4; SC-003, SC-008).
 *
 * Drives a headless terminal through the US4 scenarios and prints, per
 * scenario, the bytes each frame wrote and the time spent in
 * ovd_frame_end, with the build and machine it ran on. The headless
 * backend does no terminal I/O: the numbers cover diffing and encoding,
 * plus a sink that only records the length, and say nothing about how
 * fast a real terminal displays the output. ISO C only (research.md R47).
 *
 * Usage: render_benchmark [frames]
 *   frames: measured frames per scenario, 1 to 1000000 (default 1000).
 *   Each scenario also runs one untimed setup frame first.
 *
 * Exits nonzero if any call fails, the clock steps backwards, or the
 * SC-003 worst case exceeds its 64-byte budget.
 */
#include "bench_info.h"

#include <overdrive/overdrive.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

constexpr int64_t DEFAULT_FRAMES = 1000;
constexpr int64_t MAX_FRAMES = 1000000;
constexpr size_t SC003_BUDGET = 64;

/* ---- Clock ----------------------------------------------------------- */

typedef struct Clock {
    int base;
    const char *name;
} Clock;

/* TIME_MONOTONIC is optional in C23; TIME_UTC is always there, but it is
 * a wall clock and can step. */
static Clock choose_clock(void)
{
#ifdef TIME_MONOTONIC
    struct timespec probe;
    if (timespec_get(&probe, TIME_MONOTONIC) == TIME_MONOTONIC) {
        return (Clock){ TIME_MONOTONIC, "TIME_MONOTONIC" };
    }
#endif
    return (Clock){ TIME_UTC, "TIME_UTC (wall clock)" };
}

static bool now_ns(const Clock *clock, int64_t *out)
{
    struct timespec t;
    if (timespec_get(&t, clock->base) != clock->base) {
        return false;
    }
    *out = (int64_t)t.tv_sec * 1000000000 + (int64_t)t.tv_nsec;
    return true;
}

/* The smallest nonzero step seen between consecutive reads, or -1. */
static int64_t observed_step(const Clock *clock)
{
    int64_t best = INT64_MAX;
    for (int trial = 0; trial < 200; ++trial) {
        int64_t start = 0;
        int64_t next = 0;
        if (!now_ns(clock, &start)) {
            return -1;
        }
        long spins = 0;
        do {
            if (!now_ns(clock, &next) || ++spins > 10000000) {
                return -1;
            }
        } while (next == start);
        if (next > start && next - start < best) {
            best = next - start;
        }
    }
    return best == INT64_MAX ? -1 : best;
}

/* ---- Sink ------------------------------------------------------------ */

typedef struct Sink {
    size_t length; /* bytes of the last call; reset to 0 before each frame */
} Sink;

static bool record_length(void *context, const char *, size_t length)
{
    Sink *sink = context;
    sink->length = length;
    return true;
}

/* ---- Screens --------------------------------------------------------- */

static OvdStyle style_rgb(uint32_t fg, uint32_t bg, uint16_t attrs)
{
    return (OvdStyle){ .fg = ovd_color_hex(fg), .bg = ovd_color_hex(bg), .attrs = attrs };
}

static void text(OvdCanvas *canvas, int32_t x, int32_t y, const char *s, OvdStyle style)
{
    ovd_canvas_text(canvas, x, y, s, strlen(s), style);
}

static const char *const PHRASES[] = {
    "connected to upstream; 3 requests pending",
    "render pass complete: 12000 cells compared, 41 changed",
    "warning: retrying after timeout (attempt 2 of 5)",
    "the quick brown fox jumps over the lazy dog, again and again, across a wide pane",
    "na\xc3\xafve caf\xc3\xa9 \xe2\x80\x94 \xe6\x9d\xb1\xe4\xba\xac \xe2\x86\x92 ok",
    "idle",
};
constexpr size_t PHRASE_COUNT = sizeof PHRASES / sizeof PHRASES[0];

/* The representative screen: an RGB background everywhere, a header row,
 * a light box around the body, body rows showing log lines first_line
 * onward in alternating RGB foregrounds, and a status row. */
static void draw_screen(OvdCanvas *canvas, int64_t first_line)
{
    const OvdSize size = ovd_canvas_size(canvas);
    const OvdStyle background = style_rgb(0xC8CED6, 0x10151C, OVD_ATTR_NONE);
    const OvdStyle header = style_rgb(0xFFFFFF, 0x2A4D7A, OVD_ATTR_BOLD);
    const OvdStyle frame = style_rgb(0x56606C, 0x10151C, OVD_ATTR_NONE);
    const OvdStyle body[2] = {
        style_rgb(0xC8CED6, 0x10151C, OVD_ATTR_NONE),
        style_rgb(0x8FA1B3, 0x10151C, OVD_ATTR_NONE),
    };
    const OvdStyle status = style_rgb(0x9CDCFE, 0x1C2330, OVD_ATTR_NONE);

    ovd_canvas_fill(canvas, (OvdRect){ 0, 0, size.cols, size.rows }, background);
    ovd_canvas_fill(canvas, (OvdRect){ 0, 0, size.cols, 1 }, header);
    text(canvas, 1, 0, "Overdrive render benchmark", header);
    ovd_canvas_box(canvas, (OvdRect){ 0, 1, size.cols, size.rows - 2 }, OVD_BORDER_LIGHT, frame);
    for (int32_t row = 2; row < size.rows - 2; ++row) {
        const int64_t line = first_line + (row - 2);
        char buffer[256];
        const int length = snprintf(buffer, sizeof buffer, "%08" PRId64 "  %s", line,
                                    PHRASES[(size_t)line % PHRASE_COUNT]);
        if (length > 0) {
            const size_t used = (size_t)length < sizeof buffer ? (size_t)length : sizeof buffer - 1;
            ovd_canvas_text(canvas, 2, row, buffer, used, body[line % 2]);
        }
    }
    ovd_canvas_fill(canvas, (OvdRect){ 0, size.rows - 1, size.cols, 1 }, status);
    text(canvas, 1, size.rows - 1, "status: ok", status);
}

/* Each returns false if the frame it drew is not the one it claims. */
typedef bool (*DrawFrame)(OvdCanvas *canvas, int64_t frame);

static bool draw_full(OvdCanvas *canvas, int64_t)
{
    draw_screen(canvas, 0);
    return true;
}

static bool draw_one_cell(OvdCanvas *canvas, int64_t frame)
{
    draw_screen(canvas, 0);
    text(canvas, 100, 30, frame % 2 == 0 ? "A" : "B", style_rgb(0xFFD166, 0x10151C, OVD_ATTR_NONE));
    return true;
}

static bool draw_scrolling(OvdCanvas *canvas, int64_t frame)
{
    draw_screen(canvas, frame);
    return true;
}

/* SC-003's worst case: the bottom-right cell, the longest cursor move;
 * both colors RGB with three-digit components, the longest RGB SGR; a
 * 4-byte narrow glyph (U+1D400/U+1D401, East Asian Width N); no
 * attributes. It alternates, so every frame changes exactly that cell. */
static bool draw_worst_cell(OvdCanvas *canvas, int64_t frame)
{
    const OvdSize size = ovd_canvas_size(canvas);
    const bool even = frame % 2 == 0;
    const char *glyph = even ? "\xf0\x9d\x90\x80" : "\xf0\x9d\x90\x81";
    const OvdStyle style = even ? style_rgb(0xFFFEFD, 0xFCFBFA, OVD_ATTR_NONE)
                                : style_rgb(0xFAFBFC, 0xFDFEFF, OVD_ATTR_NONE);
    const int32_t x = size.cols - 1;
    const int32_t y = size.rows - 1;
    text(canvas, x, y, glyph, style);
    OvdCellView view;
    return ovd_canvas_cell(canvas, x, y, &view) && view.width == 1 && view.length == 4;
}

/* ---- Scenarios ------------------------------------------------------- */

typedef struct Scenario {
    const char *name;
    OvdSize size;
    DrawFrame draw;
    bool invalidate; /* forget the screen before every frame: a full redraw */
} Scenario;

typedef struct Stats {
    size_t bytes_min;
    size_t bytes_max;
    uint64_t bytes_total;
    int64_t ns_min;
    int64_t ns_median;
    int64_t ns_p99;
    int64_t ns_max;
    int64_t ns_total;
} Stats;

static int compare_int64(const void *a, const void *b)
{
    const int64_t x = *(const int64_t *)a;
    const int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* The sample at nearest rank ceil(n * numerator / denominator). */
static int64_t nearest_rank(const int64_t *sorted, int64_t n, int64_t numerator,
                            int64_t denominator)
{
    int64_t rank = (n * numerator + denominator - 1) / denominator;
    if (rank < 1) {
        rank = 1;
    }
    return sorted[rank - 1];
}

static bool failed(const Scenario *s, const char *what, OvdResult result)
{
    fprintf(stderr, "render_benchmark: %s: %s: %s\n", s->name, what, ovd_result_name(result));
    return false;
}

/* One setup frame, then frames timed frames. */
static bool run_scenario(const Scenario *s, const Clock *clock, int64_t frames, int64_t *samples,
                         Stats *out)
{
    Sink sink = {};
    OvdTerminalConfig config = ovd_terminal_config_default();
    config.backend = OVD_BACKEND_HEADLESS;
    config.headless_size = s->size;
    config.color_depth = OVD_COLOR_DEPTH_TRUECOLOR;
    config.synchronized_output = false;
    config.sink = record_length;
    config.sink_context = &sink;
    OvdTerminal *terminal = nullptr;
    const OvdResult created = ovd_terminal_create(&config, &terminal);
    if (created != OVD_OK) {
        return failed(s, "create", created);
    }

    Stats stats = { .bytes_min = SIZE_MAX };
    bool ok = true;
    for (int64_t frame = 0; ok && frame <= frames; ++frame) {
        if (s->invalidate) {
            ovd_terminal_invalidate(terminal);
        }
        OvdCanvas *canvas = nullptr;
        const OvdResult begun = ovd_frame_begin(terminal, &canvas);
        if (begun != OVD_OK) {
            ok = failed(s, "begin", begun);
            break;
        }
        if (!s->draw(canvas, frame)) {
            fprintf(stderr, "render_benchmark: %s: the frame is not the one specified\n",
                    s->name);
            ovd_frame_abandon(terminal);
            ok = false;
            break;
        }
        sink.length = 0;
        int64_t start = 0;
        int64_t end = 0;
        const bool timed = now_ns(clock, &start);
        const OvdResult ended = ovd_frame_end(terminal);
        if (!timed || !now_ns(clock, &end)) {
            fprintf(stderr, "render_benchmark: %s: the clock could not be read\n", s->name);
            ok = false;
            break;
        }
        if (ended != OVD_OK) {
            ok = failed(s, "end", ended);
            break;
        }
        if (frame == 0) {
            continue; /* the setup frame */
        }
        if (end < start) {
            fprintf(stderr, "render_benchmark: %s: the clock went backwards; no result\n",
                    s->name);
            ok = false;
            break;
        }
        samples[frame - 1] = end - start;
        stats.ns_total += end - start;
        stats.bytes_total += sink.length;
        stats.bytes_min = sink.length < stats.bytes_min ? sink.length : stats.bytes_min;
        stats.bytes_max = sink.length > stats.bytes_max ? sink.length : stats.bytes_max;
    }
    ovd_terminal_destroy(terminal);
    if (!ok) {
        return false;
    }
    qsort(samples, (size_t)frames, sizeof samples[0], compare_int64);
    stats.ns_min = samples[0];
    stats.ns_median = nearest_rank(samples, frames, 1, 2);
    stats.ns_p99 = nearest_rank(samples, frames, 99, 100);
    stats.ns_max = samples[frames - 1];
    *out = stats;
    return true;
}

static double us(int64_t ns)
{
    return (double)ns / 1000.0;
}

static void print_row(const Scenario *s, const Stats *st, int64_t frames)
{
    char size[24];
    snprintf(size, sizeof size, "%" PRId32 "x%" PRId32, s->size.cols, s->size.rows);
    printf("%-20s %-8s %8zu %9.1f %8zu  %9.2f %9.2f %9.2f %9.2f %9.2f\n", s->name, size,
           st->bytes_min, (double)st->bytes_total / (double)frames, st->bytes_max,
           us(st->ns_min), us(st->ns_median), (double)st->ns_total / (double)frames / 1000.0,
           us(st->ns_p99), us(st->ns_max));
}

/* ---- Main ------------------------------------------------------------ */

static bool parse_frames(const char *arg, int64_t *out)
{
    if (arg[0] < '0' || arg[0] > '9') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const long long value = strtoll(arg, &end, 10);
    if (errno != 0 || *end != '\0' || value < 1 || value > MAX_FRAMES) {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static void print_header(const Clock *clock, int64_t step, int64_t frames)
{
    const OvdVersion version = ovd_version();
    char when[32] = "unknown";
    const time_t now = time(nullptr);
    const struct tm *utc = now == (time_t)-1 ? nullptr : gmtime(&now);
    if (utc != nullptr) {
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S UTC", utc);
    }
    printf("Overdrive render benchmark\n");
    printf("  library    %" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n", (uint32_t)version.major,
           (uint32_t)version.minor, (uint32_t)version.patch);
    printf("  build      %s, C flags \"%s\"\n", BENCH_BUILD_TYPE, BENCH_C_FLAGS);
    printf("  compiler   %s\n", BENCH_COMPILER);
    printf("  target     %s\n", BENCH_TARGET);
    printf("  machine    %s; %s physical cores; %s MiB; %s (configure host)\n", BENCH_HOST_CPU,
           BENCH_HOST_CORES, BENCH_HOST_MEMORY_MIB, BENCH_HOST_SYSTEM);
    if (step > 0) {
        printf("  clock      %s; smallest observed step %" PRId64 " ns\n", clock->name, step);
    } else {
        printf("  clock      %s; step not observed\n", clock->name);
    }
    printf("  run        %s\n", when);
    printf("  frames     %" PRId64 " timed per scenario, after 1 untimed setup frame\n", frames);
    printf("  terminal   headless, truecolor, synchronized output off\n\n");
    printf("Bytes: written per frame. Time: ovd_frame_end per frame, in microseconds;\n"
           "median and p99 by nearest rank.\n\n");
    printf("%-20s %-8s %8s %9s %8s  %9s %9s %9s %9s %9s\n", "scenario", "size", "bytes min",
           "mean", "max", "min", "median", "mean", "p99", "max");
}

int main(int argc, char **argv)
{
    int64_t frames = DEFAULT_FRAMES;
    if (argc > 2 || (argc == 2 && !parse_frames(argv[1], &frames))) {
        fprintf(stderr, "usage: render_benchmark [frames]   (1 to %" PRId64 ", default %" PRId64
                        ")\n",
                MAX_FRAMES, DEFAULT_FRAMES);
        return EXIT_FAILURE;
    }
    const Scenario scenarios[] = {
        { "full redraw", { 200, 60 }, draw_full, true },
        { "one-cell change", { 200, 60 }, draw_one_cell, false },
        { "scrolling region", { 200, 60 }, draw_scrolling, false },
        { "no change", { 200, 60 }, draw_full, false },
        { "SC-003 worst cell", { 999, 999 }, draw_worst_cell, false },
    };
    constexpr size_t count = sizeof scenarios / sizeof scenarios[0];

    int64_t *samples = malloc((size_t)frames * sizeof *samples);
    if (samples == nullptr) {
        fprintf(stderr, "render_benchmark: no memory for %" PRId64 " samples\n", frames);
        return EXIT_FAILURE;
    }
    const Clock clock = choose_clock();
    print_header(&clock, observed_step(&clock), frames);

    Stats stats[count];
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = run_scenario(&scenarios[i], &clock, frames, samples, &stats[i]);
        if (ok) {
            print_row(&scenarios[i], &stats[i], frames);
            fflush(stdout);
        }
    }
    free(samples);
    if (!ok) {
        return EXIT_FAILURE;
    }

    const Stats *worst = &stats[count - 1];
    const bool within = worst->bytes_max <= SC003_BUDGET;
    printf("\nSC-003: one narrow cell, both colors RGB, no attributes, at 999x999: "
           "at most %zu bytes per frame (budget %zu): %s\n",
           worst->bytes_max, SC003_BUDGET, within ? "within budget" : "OVER BUDGET");
    printf("SC-008: full 200x60 redraw in ovd_frame_end: median %.2f us, p99 %.2f us "
           "(no target set yet)\n",
           us(stats[0].ns_median), us(stats[0].ns_p99));
    return within ? EXIT_SUCCESS : EXIT_FAILURE;
}
