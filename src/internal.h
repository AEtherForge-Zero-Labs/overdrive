/*
 * Terminal state (private). FR-002, FR-010 to FR-014, FR-022; research.md
 * R21 to R29, R31, R35, R44, R46.
 *
 * Owning module of struct OvdTerminal (terminal.c). The struct is
 * declared here for the terminal module's own files; only they read or
 * change its fields. The system backend's state, OvdTerminalSystem, is
 * owned by terminal_system.c, the only terminal file that calls the
 * operating-system layer (platform/platform.h).
 */
#ifndef OVERDRIVE_SRC_INTERNAL_H
#define OVERDRIVE_SRC_INTERNAL_H

#include "alloc.h"
#include "buffer.h"
#include "output.h"
#include "platform/platform.h"

#include <overdrive/render.h>
#include <overdrive/terminal.h>

#include <stddef.h>
#include <stdint.h>

/* ---- System backend (terminal_system.c) ----------------------------- */

/* What a system terminal changed at create, so destroy can undo it
 * (FR-011, R26, R28). Declared here so struct OvdTerminal embeds it;
 * only terminal_system.c reads or writes its fields.
 *
 * Invariants:
 * - modes records exactly the R28 steps 2 to 4 that open completed and no
 *   undo, enter failure, or close has reversed since;
 * - alternate_screen is the config's, and the exit sequence leaves the
 *   alternate screen exactly when it is set.
 * Zero-initialized means nothing is recorded. */
typedef struct OvdTerminalSystem {
    OvdPlatformModes modes;
    bool alternate_screen;
} OvdTerminalSystem;

/* R28 steps 1 to 5, in order: stdout must be a terminal; the modes change
 * (platform.h's ovd_platform_enter); the size is read into *size. system
 * must be zero-initialized. Returns:
 * - OVD_ERROR_NOT_A_TERMINAL if stdout is not a terminal, with nothing
 *   written or changed;
 * - OVD_ERROR_PLATFORM if a mode cannot be changed or the size cannot be
 *   read or reads as 0 (R25), with every completed step undone.
 * On OVD_OK, *system records the changed modes and alternate_screen. */
[[nodiscard]] OvdResult ovd_terminal_system_open(OvdTerminalSystem *system,
                                                 bool alternate_screen, OvdSize *size);

/* The color depth the environment asks for (detect.h), with the Windows
 * virtual-terminal flag taken from what open recorded. */
[[nodiscard]] OvdColorDepth ovd_terminal_system_detect(const OvdTerminalSystem *system);

/* Undoes R28 steps 4, 3, 2, writing nothing: create's rollback when a
 * step after open but before enter fails. */
void ovd_terminal_system_undo(OvdTerminalSystem *system);

/* R28 step 7: writes the enter sequence in one write. On failure, part of
 * it may have reached the terminal, so the exit sequence is written
 * best-effort, leaving the alternate screen whenever it was requested and
 * moving to the last of `rows` otherwise; steps 4, 3, 2 are undone; and
 * the result is OVD_ERROR_IO. rows is the size read at create. */
[[nodiscard]] OvdResult ovd_terminal_system_enter(OvdTerminalSystem *system, int32_t rows);

/* Destroy's restore (R26, R28): the exit sequence in one write, then
 * steps 4, 3, 2. rows is the last ended frame's row count, or the size
 * read at create if none has ended. Every step is attempted even if an
 * earlier one fails; nothing is reported. */
void ovd_terminal_system_close(OvdTerminalSystem *system, int32_t rows);

/* The terminal's size, re-read for each frame (FR-014). OVD_ERROR_PLATFORM,
 * leaving *size unchanged, if it cannot be read or reads as 0 (R25). */
[[nodiscard]] OvdResult ovd_terminal_system_size(OvdSize *size);

/* Writes one frame's bytes to stdout with nothing interleaved (FR-022).
 * False if not every byte was accepted. */
[[nodiscard]] bool ovd_terminal_system_write(const char *bytes, size_t length);

/* ---- The terminal (terminal.c) --------------------------------------- */

/* Invariants:
 * - self is the block this struct lives in, from allocator;
 * - grids[0] and grids[1] are both of size, and output's capacity is
 *   ovd_diff_bound for size and synchronized;
 * - grids[screen] is what the terminal shows when screen_known; the
 *   other grid is the frame, and canvas is open on it exactly while
 *   frame_open;
 * - sink_running implies !frame_open (end closes the frame before the
 *   sink runs, R24);
 * - has_pending implies pending has positive components, and only the
 *   headless backend sets it;
 * - last_rows is positive: the rows of the last frame passed to
 *   ovd_frame_end, or of the size at create if none has been (R26, R46);
 * - system is used only by the system backend, and records what its
 *   create changed until destroy restores it. */
struct OvdTerminal {
    OvdAllocator allocator;
    OvdBlock self;

    OvdBackend backend;
    OvdColorDepth depth;
    bool synchronized;
    OvdSink sink;
    void *sink_context;

    OvdSize size;
    OvdSize pending;
    bool has_pending;
    int32_t last_rows;

    OvdBuffer grids[2];
    size_t screen;
    bool screen_known;
    OvdOutput output;
    OvdCanvas canvas;

    bool frame_open;
    bool sink_running;

    OvdTerminalSystem system;
};

#endif /* OVERDRIVE_SRC_INTERNAL_H */
