/*
 * input_demo: Overdrive's input demonstration (feature 002, T121, SC-006;
 * the name is from OVD-IDENTITY-001 §8.3).
 *
 * Acquires the process's terminal with input on and shows the last events
 * it reads: keys with their modifiers, mouse actions at a cell (the last
 * one is marked), pastes, focus, and resizes. docs/validation/
 * manual-checklist.md, part 2, says what to check by hand.
 *
 *     input_demo [--mouse] [--paste] [--focus] [--crash=abort|segv]
 *     input_demo --no-input
 *
 * --mouse, --paste, and --focus turn on the matching report. q quits.
 * With --crash, pressing c ends the process abnormally, so the checklist
 * can check that the terminal is restored anyway: abort calls abort, and
 * segv raises SIGSEGV on POSIX and makes an access violation on Windows.
 * --no-input leaves input off and draws a static screen; Ctrl+C then ends
 * the process, and the library's restore_on_exit restores the terminal
 * (the checklist's Windows check of Ctrl+C with input off).
 *
 * The demo waits for events without spinning (ovd_input_wait) and draws a
 * frame after each. --no-input sleeps between redraws. It installs no
 * signal handler of its own. Public API and ISO C only, apart from the
 * Windows access violation and the sleep (nanosleep, or Windows' Sleep).
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* nanosleep */
#endif

#include <overdrive/overdrive.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* OVD-IDENTITY-001 §6.1. */
constexpr uint32_t CARBON = 0x0B1018;
constexpr uint32_t GRAPHITE = 0x1C2736;
constexpr uint32_t IGNITION = 0xFF9138;
constexpr uint32_t ELECTRIC = 0x49B9FF;
constexpr uint32_t SIGNAL = 0x63E4AE;
constexpr uint32_t FOREGROUND = 0xF5F7FB;
constexpr uint32_t MUTED = 0xAAB8CC;

/* The log: the last LOG_LINES events, one line of at most LINE_SIZE - 1
 * bytes each. A paste's first line is kept to PASTE_PREVIEW - 1 bytes. */
constexpr size_t LOG_LINES = 12;
constexpr size_t LINE_SIZE = 112;
constexpr size_t PASTE_PREVIEW = 48;

typedef enum Crash : uint8_t {
    CRASH_NONE = 0,
    CRASH_ABORT,
    CRASH_SEGV,
} Crash;

typedef struct Options {
    bool input;
    bool mouse;
    bool paste;
    bool focus;
    Crash crash;
} Options;

typedef struct Demo {
    Options options;
    char log[LOG_LINES][LINE_SIZE];
    size_t log_count; /* lines filled, at most LOG_LINES */
    size_t log_next;  /* the slot the next line goes into */
    bool has_mouse;
    int32_t mouse_x;
    int32_t mouse_y;
    /* The paste in progress. */
    size_t paste_length;
    char paste_line[PASTE_PREVIEW];
    size_t paste_line_length;
    bool paste_line_done;
    uint64_t events;
} Demo;

/* Each option at most once; --no-input excludes the others. */
static bool parse_options(int argc, char *argv[], Options *out)
{
    Options options = { .input = true };
    bool no_input = false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--mouse") == 0 && !options.mouse) {
            options.mouse = true;
        } else if (strcmp(arg, "--paste") == 0 && !options.paste) {
            options.paste = true;
        } else if (strcmp(arg, "--focus") == 0 && !options.focus) {
            options.focus = true;
        } else if (strcmp(arg, "--crash=abort") == 0 && options.crash == CRASH_NONE) {
            options.crash = CRASH_ABORT;
        } else if (strcmp(arg, "--crash=segv") == 0 && options.crash == CRASH_NONE) {
            options.crash = CRASH_SEGV;
        } else if (strcmp(arg, "--no-input") == 0 && !no_input) {
            no_input = true;
        } else {
            return false;
        }
    }
    if (no_input) {
        if (options.mouse || options.paste || options.focus || options.crash != CRASH_NONE) {
            return false;
        }
        options.input = false;
    }
    *out = options;
    return true;
}

static OvdStyle style_of(uint32_t fg, uint32_t bg, uint16_t attrs)
{
    return (OvdStyle){ .fg = ovd_color_hex(fg), .bg = ovd_color_hex(bg), .attrs = attrs };
}

static void put(OvdCanvas *canvas, int32_t x, int32_t y, const char *text, OvdStyle style)
{
    ovd_canvas_text(canvas, x, y, text, strlen(text), style);
}

/* Appends a line to the log, replacing the oldest when it is full. */
static void log_line(Demo *demo, const char *text)
{
    snprintf(demo->log[demo->log_next], LINE_SIZE, "%s", text);
    demo->log_next = (demo->log_next + 1) % LOG_LINES;
    if (demo->log_count < LOG_LINES) {
        ++demo->log_count;
    }
}

/* Writes the UTF-8 encoding of a Unicode scalar value, NUL-terminated,
 * into out[5]. */
static void encode_utf8(uint32_t codepoint, char out[5])
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        out[1] = '\0';
    } else if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
    } else if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
    } else {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        out[4] = '\0';
    }
}

static const char *key_name(OvdKey key)
{
    switch (key) {
    case OVD_KEY_CHAR:
        return "char";
    case OVD_KEY_ENTER:
        return "Enter";
    case OVD_KEY_TAB:
        return "Tab";
    case OVD_KEY_BACKSPACE:
        return "Backspace";
    case OVD_KEY_ESCAPE:
        return "Escape";
    case OVD_KEY_UP:
        return "Up";
    case OVD_KEY_DOWN:
        return "Down";
    case OVD_KEY_LEFT:
        return "Left";
    case OVD_KEY_RIGHT:
        return "Right";
    case OVD_KEY_HOME:
        return "Home";
    case OVD_KEY_END:
        return "End";
    case OVD_KEY_PAGE_UP:
        return "PageUp";
    case OVD_KEY_PAGE_DOWN:
        return "PageDown";
    case OVD_KEY_INSERT:
        return "Insert";
    case OVD_KEY_DELETE:
        return "Delete";
    case OVD_KEY_F1:
        return "F1";
    case OVD_KEY_F2:
        return "F2";
    case OVD_KEY_F3:
        return "F3";
    case OVD_KEY_F4:
        return "F4";
    case OVD_KEY_F5:
        return "F5";
    case OVD_KEY_F6:
        return "F6";
    case OVD_KEY_F7:
        return "F7";
    case OVD_KEY_F8:
        return "F8";
    case OVD_KEY_F9:
        return "F9";
    case OVD_KEY_F10:
        return "F10";
    case OVD_KEY_F11:
        return "F11";
    case OVD_KEY_F12:
        return "F12";
    }
    return "unknown key";
}

/* "Shift+Alt+Ctrl+", or the part of it that applies. */
static void modifier_prefix(uint8_t modifiers, char out[24])
{
    snprintf(out, 24, "%s%s%s", (modifiers & OVD_MODIFIER_SHIFT) != 0 ? "Shift+" : "",
             (modifiers & OVD_MODIFIER_ALT) != 0 ? "Alt+" : "",
             (modifiers & OVD_MODIFIER_CTRL) != 0 ? "Ctrl+" : "");
}

/* A printable character as itself, anything else (C0, DEL, C1) as U+XXXX. */
static void describe_key(const OvdKeyEvent *key, char *out, size_t size)
{
    char prefix[24];
    modifier_prefix(key->modifiers, prefix);
    if (key->key != OVD_KEY_CHAR) {
        snprintf(out, size, "key   %s%s", prefix, key_name(key->key));
        return;
    }
    const uint32_t c = key->codepoint;
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0)) {
        snprintf(out, size, "key   %sU+%04lX", prefix, (unsigned long)c);
        return;
    }
    char text[5];
    encode_utf8(c, text);
    snprintf(out, size, "key   %s%s  (U+%04lX)", prefix, text, (unsigned long)c);
}

static const char *mouse_action_name(OvdMouseAction action)
{
    switch (action) {
    case OVD_MOUSE_ACTION_PRESS:
        return "press";
    case OVD_MOUSE_ACTION_RELEASE:
        return "release";
    case OVD_MOUSE_ACTION_DRAG:
        return "drag";
    case OVD_MOUSE_ACTION_WHEEL:
        return "wheel";
    }
    return "unknown action";
}

static const char *mouse_button_name(OvdMouseButton button)
{
    switch (button) {
    case OVD_MOUSE_BUTTON_NONE:
        return "(no button)";
    case OVD_MOUSE_BUTTON_LEFT:
        return "left";
    case OVD_MOUSE_BUTTON_MIDDLE:
        return "middle";
    case OVD_MOUSE_BUTTON_RIGHT:
        return "right";
    case OVD_MOUSE_BUTTON_WHEEL_UP:
        return "up";
    case OVD_MOUSE_BUTTON_WHEEL_DOWN:
        return "down";
    }
    return "unknown button";
}

/* Keeps the start of the paste's first line: bytes up to the first CR or
 * LF, cut so a scalar value is never split (chunks never split one). */
static void keep_paste_line(Demo *demo, const OvdPasteText *chunk)
{
    if (demo->paste_line_done) {
        return;
    }
    size_t n = 0;
    while (n < chunk->length && chunk->bytes[n] != '\n' && chunk->bytes[n] != '\r') {
        ++n;
    }
    const bool line_ends = n < chunk->length;
    const size_t room = sizeof demo->paste_line - 1 - demo->paste_line_length;
    if (n > room) {
        n = room;
        while (n > 0 && ((unsigned char)chunk->bytes[n] & 0xC0) == 0x80) {
            --n;
        }
        demo->paste_line_done = true;
    }
    memcpy(demo->paste_line + demo->paste_line_length, chunk->bytes, n);
    demo->paste_line_length += n;
    demo->paste_line[demo->paste_line_length] = '\0';
    if (line_ends) {
        demo->paste_line_done = true;
    }
}

/* Records one event in the log. */
static void record(Demo *demo, const OvdEvent *event)
{
    char line[LINE_SIZE];
    char prefix[24];
    ++demo->events;
    switch (event->kind) {
    case OVD_EVENT_KIND_NONE:
        return;
    case OVD_EVENT_KIND_KEY:
        describe_key(&event->key, line, sizeof line);
        break;
    case OVD_EVENT_KIND_MOUSE:
        modifier_prefix(event->mouse.modifiers, prefix);
        snprintf(line, sizeof line, "mouse %s%s %s at %ld,%ld", prefix,
                 mouse_action_name(event->mouse.action), mouse_button_name(event->mouse.button),
                 (long)event->mouse.x, (long)event->mouse.y);
        demo->has_mouse = true;
        demo->mouse_x = event->mouse.x;
        demo->mouse_y = event->mouse.y;
        break;
    case OVD_EVENT_KIND_PASTE_START:
        demo->paste_length = 0;
        demo->paste_line_length = 0;
        demo->paste_line[0] = '\0';
        demo->paste_line_done = false;
        return;
    case OVD_EVENT_KIND_PASTE_TEXT:
        demo->paste_length += event->paste.length;
        keep_paste_line(demo, &event->paste);
        return;
    case OVD_EVENT_KIND_PASTE_END:
        snprintf(line, sizeof line, "paste %zu bytes, first line: %s", demo->paste_length,
                 demo->paste_line);
        break;
    case OVD_EVENT_KIND_FOCUS_IN:
        snprintf(line, sizeof line, "focus in");
        break;
    case OVD_EVENT_KIND_FOCUS_OUT:
        snprintf(line, sizeof line, "focus out");
        break;
    case OVD_EVENT_KIND_RESIZE:
        snprintf(line, sizeof line, "size  %ldx%ld", (long)event->size.cols,
                 (long)event->size.rows);
        break;
    default:
        snprintf(line, sizeof line, "event of unknown kind %u", (unsigned)event->kind);
        break;
    }
    log_line(demo, line);
}

static void draw_screen(OvdCanvas *canvas, const Demo *demo)
{
    const OvdSize size = ovd_canvas_size(canvas);
    const OvdStyle page = style_of(FOREGROUND, CARBON, OVD_ATTR_NONE);
    const OvdStyle muted = style_of(MUTED, CARBON, OVD_ATTR_NONE);
    ovd_canvas_fill(canvas, (OvdRect){ 0, 0, size.cols, size.rows }, page);

    put(canvas, 2, 1, "Overdrive", style_of(IGNITION, CARBON, OVD_ATTR_BOLD));
    put(canvas, 12, 1, "input_demo", page);
    char text[LINE_SIZE];
    const Options *o = &demo->options;
    if (o->input) {
        snprintf(text, sizeof text, "input on   mouse %s   paste %s   focus %s   size %ldx%ld",
                 o->mouse ? "on" : "off", o->paste ? "on" : "off", o->focus ? "on" : "off",
                 (long)size.cols, (long)size.rows);
    } else {
        snprintf(text, sizeof text, "input off   size %ldx%ld", (long)size.cols,
                 (long)size.rows);
    }
    put(canvas, 2, 2, text, muted);

    const int32_t top = 4;
    const int32_t rows = (int32_t)LOG_LINES + 2;
    ovd_canvas_fill(canvas, (OvdRect){ 2, top, size.cols - 4, rows },
                    style_of(FOREGROUND, GRAPHITE, OVD_ATTR_NONE));
    ovd_canvas_box(canvas, (OvdRect){ 2, top, size.cols - 4, rows }, OVD_BORDER_ROUNDED,
                   style_of(ELECTRIC, GRAPHITE, OVD_ATTR_NONE));
    put(canvas, 4, top, o->input ? " Last events " : " Input is off ",
        style_of(ELECTRIC, GRAPHITE, OVD_ATTR_NONE));
    if (!o->input) {
        put(canvas, 4, top + 1, "Ctrl+C ends the process; the terminal is restored first.",
            style_of(FOREGROUND, GRAPHITE, OVD_ATTR_NONE));
    }
    /* Oldest first, newest at the bottom. */
    const size_t first = (demo->log_next + LOG_LINES - demo->log_count) % LOG_LINES;
    for (size_t i = 0; i < demo->log_count; ++i) {
        const size_t slot = (first + i) % LOG_LINES;
        const bool newest = i + 1 == demo->log_count;
        put(canvas, 4, top + 1 + (int32_t)i, demo->log[slot],
            style_of(newest ? SIGNAL : FOREGROUND, GRAPHITE, OVD_ATTR_NONE));
    }

    if (o->input) {
        snprintf(text, sizeof text, "q quits%s   events read: %llu",
                 o->crash == CRASH_ABORT  ? "   c crashes (abort)"
                 : o->crash == CRASH_SEGV ? "   c crashes (segv)"
                                          : "",
                 (unsigned long long)demo->events);
        put(canvas, 2, top + rows + 1, text, muted);
    }

    /* The last mouse cell, in reverse video, drawn last so it shows over
     * anything there. */
    if (demo->has_mouse) {
        put(canvas, demo->mouse_x, demo->mouse_y, "+",
            style_of(IGNITION, CARBON, OVD_ATTR_REVERSE | OVD_ATTR_BOLD));
    }
}

/* Ends the process abnormally, without destroying the terminal. */
[[noreturn]] static void crash(Crash mode)
{
    if (mode == CRASH_ABORT) {
        abort();
    }
#if defined(_WIN32)
    /* An access violation, for the unhandled-exception filter. The volatile
     * pointer keeps the compiler from treating the write as unreachable. */
    int *volatile target = nullptr;
    *target = 1;
#else
    raise(SIGSEGV);
#endif
    /* Not reached if the signal or exception ends the process. */
    abort();
}

static bool draw(OvdTerminal *terminal, const Demo *demo, const char **failed_call,
                 OvdResult *result)
{
    OvdCanvas *canvas = nullptr;
    *result = ovd_frame_begin(terminal, &canvas);
    if (*result != OVD_OK) {
        *failed_call = "ovd_frame_begin";
        return false;
    }
    draw_screen(canvas, demo);
    *result = ovd_frame_end(terminal);
    if (*result != OVD_OK) {
        *failed_call = "ovd_frame_end";
        return false;
    }
    return true;
}

/* Sleeps for about 100 ms without using the processor. ISO C's only sleep
 * is thrd_sleep, and neither Apple's libc nor MinGW-w64 has <threads.h>,
 * so this is the platform's own call. A signal may end nanosleep early
 * (EINTR); the caller then only redraws sooner, so its result is unused. */
static void pause_briefly(void)
{
#if defined(_WIN32)
    Sleep(100);
#else
    const struct timespec interval = {.tv_sec = 0, .tv_nsec = 100'000'000};
    nanosleep(&interval, nullptr);
#endif
}

/* --no-input: redraw about every 100 ms, so a resize still shows, until
 * Ctrl+C ends the process. */
static bool run_without_input(OvdTerminal *terminal, const Demo *demo, const char **failed_call,
                              OvdResult *result)
{
    for (;;) {
        if (!draw(terminal, demo, failed_call, result)) {
            return false;
        }
        pause_briefly();
    }
}

int main(int argc, char *argv[])
{
    Options options;
    if (!parse_options(argc, argv, &options)) {
        fputs("usage: input_demo [--mouse] [--paste] [--focus] [--crash=abort|segv]\n"
              "       input_demo --no-input\n",
              stderr);
        return EXIT_FAILURE;
    }

    OvdTerminalConfig config = ovd_terminal_config_default();
    config.input = options.input;
    config.mouse = options.mouse;
    config.bracketed_paste = options.paste;
    config.focus_events = options.focus;
    OvdTerminal *terminal = nullptr;
    OvdResult result = ovd_terminal_create(&config, &terminal);
    if (result != OVD_OK) {
        fprintf(stderr, "input_demo: ovd_terminal_create failed: %s\n", ovd_result_name(result));
        return EXIT_FAILURE;
    }

    static Demo demo;
    demo.options = options;
    const char *failed_call = nullptr;
    if (!options.input) {
        run_without_input(terminal, &demo, &failed_call, &result);
    } else if (draw(terminal, &demo, &failed_call, &result)) {
        for (;;) {
            OvdEvent event;
            result = ovd_input_wait(terminal, OVD_WAIT_FOREVER, &event);
            if (result != OVD_OK) {
                failed_call = "ovd_input_wait";
                break;
            }
            if (event.kind == OVD_EVENT_KIND_KEY && event.key.key == OVD_KEY_CHAR
                && event.key.modifiers == 0) {
                if (event.key.codepoint == 'q') {
                    break;
                }
                if (event.key.codepoint == 'c' && options.crash != CRASH_NONE) {
                    crash(options.crash);
                }
            }
            record(&demo, &event);
            if (!draw(terminal, &demo, &failed_call, &result)) {
                break;
            }
        }
    }
    ovd_terminal_destroy(terminal);

    if (failed_call != nullptr) {
        fprintf(stderr, "input_demo: %s failed: %s\n", failed_call, ovd_result_name(result));
        return EXIT_FAILURE;
    }
    printf("input_demo: %llu events read\n", (unsigned long long)demo.events);
    return EXIT_SUCCESS;
}
