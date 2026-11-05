/*
 * hello_overdrive: Overdrive's first demonstration (T037, User Story 2).
 *
 * Acquires the process's terminal, shows a few seconds of frames, and
 * restores the terminal. Each frame shows the brand palette
 * (OVD-IDENTITY-001 §6.1), the tagline, the five border styles, and a
 * counter, size, and color depth that every frame keeps current.
 * docs/validation/manual-checklist.md says what to check by hand.
 *
 *     hello_overdrive [--synchronized-output] [--no-alternate-screen] [seconds]
 *
 * seconds is 1 to 60, default 5. The two options change the matching
 * OvdTerminalConfig fields from their defaults. They exist for the
 * checklist's checks of what a real terminal does with mode 2026 and with
 * the exit sequence used without the alternate screen (research §E, R26,
 * R34). The ZWJ row exists for its check of R38.
 *
 * Ctrl+C ends the run early. Either way the terminal is restored first;
 * then a summary line goes to stdout, or an error to stderr with exit
 * status 1.
 *
 * Public API and ISO C only. C has no portable sleep (<threads.h> is
 * optional, and the macOS SDK and MinGW-w64 lack it), so frames are paced
 * by spinning on a clock (read_clock), which keeps one core busy for the
 * run.
 */
#include <overdrive/overdrive.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

constexpr int32_t DEFAULT_SECONDS = 5;
constexpr int32_t MAX_SECONDS = 60;
constexpr int32_t FRAMES_PER_SECOND = 20;
constexpr long long NANOSECONDS_PER_SECOND = 1000000000LL;
constexpr long long FRAME_NANOSECONDS = NANOSECONDS_PER_SECOND / FRAMES_PER_SECOND;

/* The layout is a fixed block, centered; a smaller terminal clips it. */
constexpr int32_t BLOCK_COLS = 74;
constexpr int32_t BLOCK_ROWS = 20;
constexpr int32_t PANEL_ROWS = 9;
constexpr int32_t PALETTE_COLS = 24;
constexpr int32_t FRAME_COLS = 48;
constexpr int32_t BOX_COLS = 14;

/* OVD-IDENTITY-001 §6.1. */
constexpr uint32_t CARBON = 0x0B1018;
constexpr uint32_t GRAPHITE = 0x1C2736;
constexpr uint32_t IGNITION = 0xFF9138;
constexpr uint32_t ELECTRIC = 0x49B9FF;
constexpr uint32_t SIGNAL = 0x63E4AE;
constexpr uint32_t FOREGROUND = 0xF5F7FB;
constexpr uint32_t MUTED = 0xAAB8CC;

typedef struct Swatch {
    const char *token;
    uint32_t rgb;
    bool dark; /* its hex value is written in light text */
} Swatch;

static const Swatch PALETTE[] = {
    { "carbon", CARBON, true },
    { "graphite", GRAPHITE, true },
    { "ignition", IGNITION, false },
    { "electric", ELECTRIC, false },
    { "signal", SIGNAL, false },
    { "foreground", FOREGROUND, false },
    { "muted", MUTED, false },
};

typedef struct BorderSample {
    OvdBorder border;
    const char *name;
} BorderSample;

static const BorderSample BORDERS[] = {
    { OVD_BORDER_LIGHT, "light" },
    { OVD_BORDER_HEAVY, "heavy" },
    { OVD_BORDER_DOUBLE, "double" },
    { OVD_BORDER_ROUNDED, "rounded" },
    { OVD_BORDER_ASCII, "ascii" },
};

typedef struct Options {
    int32_t seconds;
    bool synchronized_output;
    bool alternate_screen;
} Options;

typedef struct Run {
    Options options;
    int32_t frame; /* 1-based */
    int32_t total;
    OvdColorDepth depth;
} Run;

static volatile sig_atomic_t stop_requested = 0;

/* ISO C lets a handler assign a volatile sig_atomic_t and call signal for
 * its own signal. Installing itself again matters where the handler is
 * reset to the default before it runs (System V semantics, the Windows C
 * runtime): a second Ctrl+C would otherwise end the process with the
 * terminal still changed. */
static void request_stop(int signal_number)
{
    stop_requested = 1;
    signal(signal_number, request_stop);
}

/* Decimal digits only, from 1 to MAX_SECONDS. */
static bool parse_seconds(const char *text, int32_t *out)
{
    int32_t value = 0;
    if (text[0] == '\0') {
        return false;
    }
    for (const char *digit = text; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        value = value * 10 + (*digit - '0');
        if (value > MAX_SECONDS) {
            return false;
        }
    }
    if (value < 1) {
        return false;
    }
    *out = value;
    return true;
}

/* Each option at most once, in any order; anything else is a usage error. */
static bool parse_options(int argc, char *argv[], Options *out)
{
    Options options = {
        .seconds = DEFAULT_SECONDS,
        .synchronized_output = false,
        .alternate_screen = true,
    };
    bool have_seconds = false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--synchronized-output") == 0 && !options.synchronized_output) {
            options.synchronized_output = true;
        } else if (strcmp(arg, "--no-alternate-screen") == 0 && options.alternate_screen) {
            options.alternate_screen = false;
        } else if (!have_seconds && parse_seconds(arg, &options.seconds)) {
            have_seconds = true;
        } else {
            return false;
        }
    }
    *out = options;
    return true;
}

/* The pacing clock, in nanoseconds from an arbitrary origin.
 *
 * Where <time.h> has timespec_get, this uses TIME_MONOTONIC (optional in
 * C23) if it works, else TIME_UTC, a wall clock that can jump, which
 * wait_until tolerates. MinGW-w64 declares timespec_get and TIME_UTC only
 * for the UCRT runtime, and Debian's cross toolchain targets msvcrt.
 * There the clock is clock(). The Microsoft C runtime documents clock()
 * as wall time since the process started. ISO C defines it as processor
 * time, which the spinning keeps close to wall time. */
static bool read_clock(long long *now)
{
#if defined(TIME_UTC)
    struct timespec t;
#if defined(TIME_MONOTONIC)
    if (timespec_get(&t, TIME_MONOTONIC) == TIME_MONOTONIC) {
        *now = (long long)t.tv_sec * NANOSECONDS_PER_SECOND + t.tv_nsec;
        return true;
    }
#endif
    if (timespec_get(&t, TIME_UTC) != TIME_UTC) {
        return false;
    }
    *now = (long long)t.tv_sec * NANOSECONDS_PER_SECOND + t.tv_nsec;
    return true;
#else
    const clock_t ticks = clock();
    if (ticks == (clock_t)-1) {
        return false;
    }
    *now = (long long)((double)ticks * ((double)NANOSECONDS_PER_SECOND / (double)CLOCKS_PER_SEC));
    return true;
#endif
}

/* Spins until target nanoseconds after start, or until Ctrl+C. A clock
 * that fails or reads earlier than start ends the wait, so a clock fault
 * cannot hang the program. */
static void wait_until(long long start, long long target)
{
    while (stop_requested == 0) {
        long long now;
        if (!read_clock(&now)) {
            return;
        }
        const long long elapsed = now - start;
        if (elapsed < 0 || elapsed >= target) {
            return;
        }
    }
}

static const char *depth_name(OvdColorDepth depth)
{
    switch (depth) {
    case OVD_COLOR_DEPTH_NONE:
        return "none (attributes only)";
    case OVD_COLOR_DEPTH_16:
        return "16 colors";
    case OVD_COLOR_DEPTH_256:
        return "256 colors";
    case OVD_COLOR_DEPTH_TRUECOLOR:
        return "truecolor";
    }
    return "unknown";
}

static OvdStyle style_of(uint32_t fg, uint32_t bg, uint16_t attrs)
{
    return (OvdStyle){ .fg = ovd_color_hex(fg), .bg = ovd_color_hex(bg), .attrs = attrs };
}

/* Draws a NUL-terminated string. */
static void put(OvdCanvas *canvas, int32_t x, int32_t y, const char *text, OvdStyle style)
{
    ovd_canvas_text(canvas, x, y, text, strlen(text), style);
}

/* A Graphite card with a border and a title set into its top edge. */
static void draw_panel(OvdCanvas *canvas, OvdRect area, OvdBorder border, uint32_t accent,
                       const char *title, uint16_t title_attrs)
{
    ovd_canvas_fill(canvas, area, style_of(FOREGROUND, GRAPHITE, OVD_ATTR_NONE));
    ovd_canvas_box(canvas, area, border, style_of(accent, GRAPHITE, OVD_ATTR_NONE));
    put(canvas, area.x + 2, area.y, title, style_of(accent, GRAPHITE, title_attrs));
}

/* Each swatch is filled cells, not block glyphs, so no font is assumed
 * (§6.3); its hex value keeps Carbon and Graphite visible on the panel. */
static void draw_palette(OvdCanvas *canvas, int32_t x, int32_t y)
{
    draw_panel(canvas, (OvdRect){ x, y, PALETTE_COLS, PANEL_ROWS }, OVD_BORDER_ROUNDED, MUTED,
               " Palette ", OVD_ATTR_NONE);
    for (size_t i = 0; i < sizeof PALETTE / sizeof PALETTE[0]; ++i) {
        const Swatch *swatch = &PALETTE[i];
        const int32_t row = y + 1 + (int32_t)i;
        char hex[16];
        snprintf(hex, sizeof hex, " #%06lX ", (unsigned long)swatch->rgb);
        put(canvas, x + 2, row, hex,
            style_of(swatch->dark ? FOREGROUND : CARBON, swatch->rgb, OVD_ATTR_NONE));
        put(canvas, x + 12, row, swatch->token, style_of(FOREGROUND, GRAPHITE, OVD_ATTR_NONE));
    }
}

/* The one Ignition Orange panel: what changes from frame to frame. */
static void draw_frame_panel(OvdCanvas *canvas, int32_t x, int32_t y, const Run *run,
                             OvdSize size)
{
    draw_panel(canvas, (OvdRect){ x, y, FRAME_COLS, PANEL_ROWS }, OVD_BORDER_HEAVY, IGNITION,
               " Frame ", OVD_ATTR_BOLD);
    const OvdStyle label = style_of(MUTED, GRAPHITE, OVD_ATTR_NONE);
    const OvdStyle value = style_of(FOREGROUND, GRAPHITE, OVD_ATTR_NONE);
    const int32_t left = x + 2;
    const int32_t column = x + 11;
    char text[64];

    put(canvas, left, y + 1, "frame", label);
    snprintf(text, sizeof text, "%ld of %ld", (long)run->frame, (long)run->total);
    put(canvas, column, y + 1, text, value);

    /* A Carbon track that fills with Signal Green as the run advances. */
    const int32_t bar_cols = FRAME_COLS - 4;
    const int32_t filled = (int32_t)((int64_t)bar_cols * run->frame / run->total);
    ovd_canvas_fill(canvas, (OvdRect){ left, y + 2, bar_cols, 1 },
                    style_of(FOREGROUND, CARBON, OVD_ATTR_NONE));
    ovd_canvas_fill(canvas, (OvdRect){ left, y + 2, filled, 1 },
                    style_of(FOREGROUND, SIGNAL, OVD_ATTR_NONE));

    put(canvas, left, y + 3, "size", label);
    snprintf(text, sizeof text, "%ldx%ld", (long)size.cols, (long)size.rows);
    put(canvas, column, y + 3, text, value);

    put(canvas, left, y + 4, "colors", label);
    put(canvas, column, y + 4, depth_name(run->depth), value);

    put(canvas, left, y + 5, "screen", label);
    put(canvas, column, y + 5, run->options.alternate_screen ? "alternate" : "main", value);

    put(canvas, left, y + 6, "sync", label);
    put(canvas, column, y + 6, run->options.synchronized_output ? "mode 2026" : "off", value);

    const OvdVersion version = ovd_version();
    put(canvas, left, y + 7, "library", label);
    snprintf(text, sizeof text, "Overdrive %u.%u.%u", (unsigned)version.major,
             (unsigned)version.minor, (unsigned)version.patch);
    put(canvas, column, y + 7, text, value);
}

/* R38: a cell whose grapheme ends in ZWJ (U+1F469 WOMAN, U+200D), then a
 * glyph (U+1F467 GIRL) drawn by a separate call in the next cell. The
 * renderer moves the cursor between the two, which research assumes stops
 * a terminal from joining them into one glyph. */
static void draw_zwj_check(OvdCanvas *canvas, int32_t x, int32_t y)
{
    static const char woman_zwj[] = "\xF0\x9F\x91\xA9\xE2\x80\x8D";
    static const char girl[] = "\xF0\x9F\x91\xA7";
    const OvdStyle text = style_of(FOREGROUND, CARBON, OVD_ATTR_NONE);
    put(canvas, x, y, "zwj check", style_of(MUTED, CARBON, OVD_ATTR_NONE));
    ovd_canvas_text(canvas, x + 11, y, woman_zwj, sizeof woman_zwj - 1, text);
    ovd_canvas_text(canvas, x + 13, y, girl, sizeof girl - 1, text);
}

static void draw_borders(OvdCanvas *canvas, int32_t x, int32_t y)
{
    for (size_t i = 0; i < sizeof BORDERS / sizeof BORDERS[0]; ++i) {
        const int32_t left = x + (int32_t)i * (BOX_COLS + 1);
        const int32_t width = (int32_t)strlen(BORDERS[i].name);
        ovd_canvas_box(canvas, (OvdRect){ left, y, BOX_COLS, 3 }, BORDERS[i].border,
                       style_of(ELECTRIC, CARBON, OVD_ATTR_NONE));
        put(canvas, left + (BOX_COLS - width) / 2, y + 1, BORDERS[i].name,
            style_of(FOREGROUND, CARBON, OVD_ATTR_NONE));
    }
}

/* Draws the whole screen; the canvas starts each frame blank. */
static void draw_screen(OvdCanvas *canvas, const Run *run)
{
    const OvdSize size = ovd_canvas_size(canvas);
    const int32_t x = size.cols > BLOCK_COLS ? (size.cols - BLOCK_COLS) / 2 : 0;
    const int32_t y = size.rows > BLOCK_ROWS ? (size.rows - BLOCK_ROWS) / 2 : 0;
    const OvdStyle page = style_of(FOREGROUND, CARBON, OVD_ATTR_NONE);

    ovd_canvas_fill(canvas, (OvdRect){ 0, 0, size.cols, size.rows }, page);
    put(canvas, x, y, "Overdrive", style_of(IGNITION, CARBON, OVD_ATTR_BOLD));
    put(canvas, x, y + 1, "Every frame matters.", page);
    draw_palette(canvas, x, y + 3);
    draw_frame_panel(canvas, x + PALETTE_COLS + 2, y + 3, run, size);
    draw_borders(canvas, x, y + 4 + PANEL_ROWS);
    draw_zwj_check(canvas, x, y + BLOCK_ROWS - 3);

    char footer[64];
    snprintf(footer, sizeof footer, "Runs for %ld s; Ctrl+C stops it early.",
             (long)run->options.seconds);
    put(canvas, x, y + BLOCK_ROWS - 1, footer, style_of(MUTED, CARBON, OVD_ATTR_NONE));
}

int main(int argc, char *argv[])
{
    Options options;
    if (!parse_options(argc, argv, &options)) {
        fprintf(stderr,
                "usage: hello_overdrive [--synchronized-output] [--no-alternate-screen] "
                "[seconds]\n"
                "       seconds: 1 to %ld, default %ld\n",
                (long)MAX_SECONDS, (long)DEFAULT_SECONDS);
        return EXIT_FAILURE;
    }
    if (signal(SIGINT, request_stop) == SIG_ERR) {
        fputs("hello_overdrive: cannot catch Ctrl+C, which would leave the terminal changed\n",
              stderr);
        return EXIT_FAILURE;
    }
    long long start;
    if (!read_clock(&start)) {
        fputs("hello_overdrive: no clock to pace frames by\n", stderr);
        return EXIT_FAILURE;
    }

    OvdTerminalConfig config = ovd_terminal_config_default();
    config.synchronized_output = options.synchronized_output;
    config.alternate_screen = options.alternate_screen;
    OvdTerminal *terminal = nullptr;
    OvdResult result = ovd_terminal_create(&config, &terminal);
    if (result != OVD_OK) {
        fprintf(stderr, "hello_overdrive: ovd_terminal_create failed: %s\n",
                ovd_result_name(result));
        return EXIT_FAILURE;
    }

    Run run = {
        .options = options,
        .frame = 0,
        .total = options.seconds * FRAMES_PER_SECOND,
        .depth = ovd_terminal_color_depth(terminal),
    };
    const char *failed_call = nullptr;
    int32_t shown = 0;
    for (int32_t frame = 1; frame <= run.total && stop_requested == 0; ++frame) {
        OvdCanvas *canvas = nullptr;
        result = ovd_frame_begin(terminal, &canvas);
        if (result != OVD_OK) {
            failed_call = "ovd_frame_begin";
            break;
        }
        run.frame = frame;
        draw_screen(canvas, &run);
        result = ovd_frame_end(terminal);
        if (result != OVD_OK) {
            failed_call = "ovd_frame_end";
            break;
        }
        shown = frame;
        wait_until(start, frame * FRAME_NANOSECONDS);
    }
    const OvdSize size = ovd_terminal_size(terminal);
    ovd_terminal_destroy(terminal);

    if (failed_call != nullptr) {
        fprintf(stderr, "hello_overdrive: %s failed: %s\n", failed_call,
                ovd_result_name(result));
        return EXIT_FAILURE;
    }
    printf("hello_overdrive: %ld frames at %ldx%ld, %s%s\n", (long)shown, (long)size.cols,
           (long)size.rows, depth_name(run.depth),
           stop_requested != 0 ? ", stopped by Ctrl+C" : "");
    return EXIT_SUCCESS;
}
