/*
 * Overdrive terminal lifecycle: acquiring a terminal (or a headless
 * stand-in), its size and capabilities, and restoring it.
 *
 * PROPOSED CONTRACT (specs/002-input-restore; changes 001's header only in
 * this comment, OvdTerminalConfig's input and restoration fields, and the
 * create and destroy comments). Experimental for 0.x.
 *
 * Threading: an OvdTerminal and everything obtained from it belong to one
 * thread at a time. Overdrive keeps no process-wide state except the
 * signal and console handlers a system terminal installs (input and
 * restore_on_exit below), so any number of headless terminals may
 * coexist. Rendering does not detect a second system terminal: each
 * restores what it saw at its own create, so system terminals must be
 * destroyed in reverse order of creation, and only one may render. On an
 * abnormal exit, the state before the first one's create is restored.
 */
#ifndef OVERDRIVE_TERMINAL_H
#define OVERDRIVE_TERMINAL_H

#include <overdrive/core.h>

typedef struct OvdTerminal OvdTerminal;

/* How much color the output may use. Colors richer than the depth are
 * mapped to the nearest color the depth allows; at OVD_COLOR_DEPTH_NONE
 * only text attributes are emitted. */
typedef enum OvdColorDepth : uint8_t {
    OVD_COLOR_DEPTH_NONE = 0,
    OVD_COLOR_DEPTH_16,
    OVD_COLOR_DEPTH_256,
    OVD_COLOR_DEPTH_TRUECOLOR,
} OvdColorDepth;

typedef enum OvdBackend : uint8_t {
    /* The process's own console (stdout, and stdin for its modes). */
    OVD_BACKEND_SYSTEM = 0,
    /* No operating-system terminal: a fixed size the caller sets, and
     * output delivered to a sink. Used by tests and snapshot tools. */
    OVD_BACKEND_HEADLESS,
} OvdBackend;

/* Receives one frame's output. Called at most once per ovd_frame_end,
 * with length > 0, after the canvas is closed. Return false if the bytes
 * could not be delivered; the frame then fails with OVD_ERROR_IO and the
 * next frame is a full redraw.
 *
 * While the sink runs, its terminal is busy: ovd_frame_begin,
 * ovd_frame_end, and ovd_terminal_set_size return OVD_ERROR_STATE;
 * ovd_frame_abandon and ovd_terminal_invalidate do nothing; the
 * terminal's canvas is already closed, so ovd_canvas_* calls on it act as
 * on a closed canvas (render.h); ovd_terminal_size and
 * ovd_terminal_color_depth return their normal values. Calling
 * ovd_terminal_destroy on it from the sink is undefined. */
typedef bool (*OvdSink)(void *context, const char *bytes, size_t length);

/* Optional replacement for the C library allocator. Overdrive allocates
 * only in ovd_terminal_create and in an ovd_frame_begin whose size differs
 * from the terminal's current buffers. Drawing, ovd_frame_end, and every
 * other call never allocate, whatever a frame contains: the output buffer
 * is sized at each size to the worst case of one frame.
 *
 * allocate is never called with size 0, and returns memory aligned for
 * any object type, or nullptr. release receives a block allocate returned,
 * never nullptr, with the size that was requested, so arena or counting
 * allocators need no headers. A resize allocates every new block before
 * releasing any old one. Both functions must be non-null. */
typedef struct OvdAllocator {
    void *(*allocate)(void *context, size_t size);
    void (*release)(void *context, void *block, size_t size);
    void *context;
} OvdAllocator;

/* Obtain with ovd_terminal_config_default(), then change fields. Fields
 * added in later versions get their defaults from that function, so a
 * config built this way stays valid across source upgrades. */
typedef struct OvdTerminalConfig {
    OvdBackend backend;

    /* System backend only. If true (default), the color depth is
     * detected from the environment (NO_COLOR, COLORTERM, TERM) and
     * color_depth is ignored. The headless backend never reads the
     * environment: it always uses color_depth. */
    bool detect_color_depth;
    /* Default OVD_COLOR_DEPTH_TRUECOLOR. Validated even when detection
     * will override it. */
    OvdColorDepth color_depth;

    /* System backend only. Use the alternate screen so the user's screen
     * contents and scrollback are untouched (default true). If false, the
     * last frame stays on screen after destroy, with the cursor at column
     * 0 of its last row. */
    bool alternate_screen;
    /* Wrap each frame in synchronized-output markers (DEC mode 2026).
     * Off by default: support is not detected in this version. */
    bool synchronized_output;

    /* Headless backend only, and validated only for it: the initial size
     * (default 80x24) and the output sink. sink may be nullptr to discard
     * output. */
    OvdSize headless_size;
    OvdSink sink;
    void *sink_context;

    /* nullptr selects malloc and free. Copied at create; the functions
     * and context must outlive the terminal. */
    const OvdAllocator *allocator;

    /* Read input (input.h). Default false: the input side is as in
     * 001 (echo off, nothing read). System backend: stdin must then be a
     * terminal, and is put in raw mode: no echo, no line editing, and
     * Ctrl+C, Ctrl+\, and Ctrl+Z arrive as keys, not signals. On POSIX it
     * also installs a process-wide SIGWINCH handler, so a wait reports
     * resizes; an earlier SIGWINCH handler still runs, and destroy puts it
     * back as for restore_on_exit. */
    bool input;
    /* Each requires input; default false. mouse: presses, releases,
     * drags, and wheel steps (no hover); the terminal's own selection is
     * unavailable while on. bracketed_paste: pastes arrive as paste
     * events, not keys. focus_events: focus in and out. */
    bool mouse;
    bool bracketed_paste;
    bool focus_events;
    /* How long a lone ESC waits for a following byte before it is the
     * Escape key, in milliseconds, 0 to 1000. Default 50. Validated even
     * when input is off. */
    int32_t escape_delay_ms;

    /* System backend only. Default true: until destroy, a crash or a
     * termination signal restores the terminal before the process ends
     * (POSIX SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGABRT, SIGSEGV, SIGBUS,
     * SIGFPE, SIGILL; Windows console close, Ctrl+C while input is off,
     * Ctrl+Break, unhandled exceptions, and abort). This installs
     * process-wide handlers, the one exception to Overdrive keeping no
     * process-wide state. Handlers installed before create still run: for
     * a termination signal the application already handles (on Windows,
     * Ctrl+C or Ctrl+Break with a C runtime signal handler), Overdrive
     * only calls that handler and restores nothing; signals ignored before
     * create stay ignored. Destroy puts back the earlier handler of each
     * signal whose handler is still Overdrive's; one the application
     * installed after create stays, and disables restoration for that
     * signal meanwhile. Only the first system terminal that is alive
     * installs them. Not covered: uncatchable signals, a normal exit
     * without destroy, a signal delivered to another thread while create
     * or destroy runs, a fault raised inside create or destroy, on macOS
     * a crash handler installed with SA_RESETHAND that returns from a real
     * fault (it runs again instead of the fault ending the process), and
     * on Windows logoff and shutdown (only services receive them) and a
     * console control handler registered directly before create (it runs
     * after the restore). On Windows the C runtime decides the claim: if
     * the application called signal() for SIGINT or SIGBREAK before
     * create, Ctrl+C and Ctrl+Break restore even when it handles them; a
     * SIGBREAK handler also claims console close; and a Ctrl+C arriving
     * while create runs may not reach the application's handler. False
     * installs none of these; input's SIGWINCH
     * handler is separate. Set false if the application manages these
     * signals itself. */
    bool restore_on_exit;
} OvdTerminalConfig;

OVD_API OvdTerminalConfig ovd_terminal_config_default(void);

/* Acquires the terminal. For the system backend, in this order: requires
 * stdout to be a terminal (writing nothing otherwise); on Windows, enables
 * virtual-terminal output with DISABLE_NEWLINE_AUTO_RETURN and sets the
 * UTF-8 output code page; turns off input echo if stdin is a terminal
 * (skipped, not an error, if it is not); reads the size; allocates; then,
 * in one write, enters the alternate screen if requested, hides the
 * cursor, and disables autowrap. Each operating-system mode is read
 * before it is changed, so destroy can restore it. With input on, raw
 * input mode follows echo, and the enter sequence also turns on the
 * requested reports (mouse, bracketed paste, focus). With
 * restore_on_exit, the handlers are installed last. The headless backend
 * changes no modes and writes nothing. Either backend allocates its one
 * canvas here, and, with input on, its fixed input buffers.
 *
 * Fails with OVD_ERROR_NOT_A_TERMINAL if stdout is not a terminal, or
 * input is on and stdin is not; OVD_ERROR_INVALID_ARGUMENT for nullptr
 * arguments, an out-of-range backend or color depth, a headless size with
 * a non-positive component, an allocator with a nullptr function, mouse,
 * bracketed_paste, or focus_events without input, or an escape delay
 * outside 0 to 1000; OVD_ERROR_ALLOCATION, including
 * when the size makes a buffer size overflow; OVD_ERROR_PLATFORM,
 * including a system terminal whose size reads as 0; or OVD_ERROR_IO if
 * the enter sequence could not be written, in which case destroy's exit
 * sequence is written best-effort first, leaving the alternate screen
 * whenever it was requested. On failure every completed step is undone
 * in reverse order, nothing remains changed, and *out is untouched. */
[[nodiscard]] OVD_API OvdResult ovd_terminal_create(const OvdTerminalConfig *config,
                                                    OvdTerminal **out);

/* Abandons an open frame, restores what create changed, and frees the
 * terminal. If a signal handler already restored the terminal, the
 * restore is not repeated; the handlers are put back either way. For the
 * system backend it writes, in one write, CAN (cancelling any sequence a
 * signal cut short), SGR reset, the report modes create turned on, turned
 * off, autowrap on, cursor visible, and then leave the alternate screen
 * if it was entered, or else move the cursor to column 0 of the last
 * frame's last row (of the size read at create if no frame has ended);
 * then restores raw input mode and input echo, the code page, and the
 * output mode to the values read at create, in that order; then puts back
 * the signal and console handlers create replaced. Every step is
 * attempted even if an earlier one fails. Cursor visibility and autowrap are set to their
 * defaults, not to earlier values: they cannot be read back without input.
 * nullptr does nothing. Not async-signal-safe. If the process exits
 * normally without calling it, the terminal stays as create left it; on
 * a crash or termination signal, restore_on_exit covers it. */
OVD_API void ovd_terminal_destroy(OvdTerminal *terminal);

/* The size of the terminal's current buffers (set at create and by each
 * ovd_frame_begin that succeeds), or, if ovd_terminal_set_size has stored
 * a size no begin has applied yet, that pending size. A failed
 * ovd_frame_begin changes neither: the pending size is kept and returned
 * until a begin applies it. The system backend does not re-read the size
 * here; the next ovd_frame_begin does. */
OVD_API OvdSize ovd_terminal_size(const OvdTerminal *terminal);

OVD_API OvdColorDepth ovd_terminal_color_depth(const OvdTerminal *terminal);

/* Headless backend only (OVD_ERROR_UNSUPPORTED otherwise): changes the
 * size, as a resize would. Takes effect, and allocates, at the next
 * ovd_frame_begin. Fails with OVD_ERROR_INVALID_ARGUMENT for nullptr or a
 * non-positive component, or OVD_ERROR_STATE while a frame is open. */
[[nodiscard]] OVD_API OvdResult ovd_terminal_set_size(OvdTerminal *terminal, OvdSize size);

/* Forgets what is on screen, so the next frame is a full redraw. Call
 * after anything else has written to the terminal. */
OVD_API void ovd_terminal_invalidate(OvdTerminal *terminal);

#endif /* OVERDRIVE_TERMINAL_H */
