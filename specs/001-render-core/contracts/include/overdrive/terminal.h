/*
 * Overdrive terminal lifecycle: acquiring a terminal (or a headless
 * stand-in), its size and capabilities, and restoring it.
 *
 * PROPOSED CONTRACT (specs/001-render-core). Experimental for 0.x.
 *
 * Threading: an OvdTerminal and everything obtained from it belong to one
 * thread at a time. Overdrive keeps no process-wide state; two terminals
 * may coexist (only one should use the system backend).
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
 * with length > 0. Return false if the bytes could not be delivered;
 * the frame then fails with OVD_ERROR_IO and the next frame is a full
 * redraw. */
typedef bool (*OvdSink)(void *context, const char *bytes, size_t length);

/* Optional replacement for the C library allocator. Overdrive allocates
 * only in ovd_terminal_create, when the terminal's size changes, and when
 * its output buffer must grow. Sizes are passed back so arena or
 * counting allocators need no headers. */
typedef struct OvdAllocator {
    void *(*allocate)(void *context, size_t size);
    void *(*reallocate)(void *context, void *block, size_t old_size, size_t new_size);
    void (*release)(void *context, void *block, size_t size);
    void *context;
} OvdAllocator;

/* Obtain with ovd_terminal_config_default(), then change fields. Fields
 * added in later versions get their defaults from that function, so a
 * config built this way stays valid across source upgrades. */
typedef struct OvdTerminalConfig {
    OvdBackend backend;

    /* If true (default), the color depth is detected from the
     * environment; color_depth is ignored. */
    bool detect_color_depth;
    OvdColorDepth color_depth;

    /* System backend only. Use the alternate screen so the user's
     * scrollback is untouched (default true). */
    bool alternate_screen;
    /* Wrap each frame in synchronized-output markers (DEC mode 2026).
     * Off by default: support is not detected in this version. */
    bool synchronized_output;

    /* Headless backend only: the initial size and the output sink.
     * sink may be nullptr to discard output. */
    OvdSize headless_size;
    OvdSink sink;
    void *sink_context;

    /* nullptr selects malloc/realloc/free. Copied at create; the
     * functions and context must outlive the terminal. */
    const OvdAllocator *allocator;
} OvdTerminalConfig;

OVD_API OvdTerminalConfig ovd_terminal_config_default(void);

/* Acquires the terminal. For the system backend this enables UTF-8 and
 * virtual-terminal output where the platform needs it, enters the
 * alternate screen if requested, hides the cursor, disables autowrap and
 * input echo, and records everything changed so destroy can undo it.
 * Input is not read in this version.
 *
 * Fails with OVD_ERROR_NOT_A_TERMINAL if stdout is not a terminal,
 * OVD_ERROR_INVALID_ARGUMENT for a headless size with a non-positive
 * component, OVD_ERROR_ALLOCATION, or OVD_ERROR_PLATFORM. On failure
 * nothing remains changed and *out is untouched. */
[[nodiscard]] OVD_API OvdResult ovd_terminal_create(const OvdTerminalConfig *config,
                                                    OvdTerminal **out);

/* Restores everything create changed, in reverse order, and frees the
 * terminal. An open frame is abandoned. nullptr does nothing. Not
 * async-signal-safe. */
OVD_API void ovd_terminal_destroy(OvdTerminal *terminal);

/* The size the next frame will have as of the last check: create, the
 * last ovd_frame_begin, or ovd_terminal_set_size. */
OVD_API OvdSize ovd_terminal_size(const OvdTerminal *terminal);

OVD_API OvdColorDepth ovd_terminal_color_depth(const OvdTerminal *terminal);

/* Headless backend only (OVD_ERROR_UNSUPPORTED otherwise): changes the
 * size, as a resize would. Takes effect at the next ovd_frame_begin.
 * Fails with OVD_ERROR_STATE while a frame is open. */
[[nodiscard]] OVD_API OvdResult ovd_terminal_set_size(OvdTerminal *terminal, OvdSize size);

/* Forgets what is on screen, so the next frame is a full redraw. Call
 * after anything else has written to the terminal. */
OVD_API void ovd_terminal_invalidate(OvdTerminal *terminal);

#endif /* OVERDRIVE_TERMINAL_H */
