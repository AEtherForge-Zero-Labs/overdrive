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
#include "decode.h"
#include "output.h"
#include "platform/platform.h"

#include <overdrive/input.h>
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
 *   undo or close has reversed since;
 * - alternate_screen is the config's, and the exit sequence leaves the
 *   alternate screen exactly when it is set;
 * - mouse, bracketed_paste, and focus_events are the config's reports
 *   (002 R116): the enter sequence turns on, and the exit sequence off,
 *   exactly these; any of them implies input.
 * Zero-initialized means nothing is recorded. */
typedef struct OvdTerminalSystem {
    OvdPlatformModes modes;
    bool alternate_screen;
    bool input;
    bool mouse;
    bool bracketed_paste;
    bool focus_events;
} OvdTerminalSystem;

/* R28 steps 1 to 5, in order: stdout must be a terminal; the modes change
 * (platform.h's ovd_platform_enter, with raw input and the resize wakeup
 * when config->input); the size is read into *size. system must be
 * zero-initialized, and config valid (terminal.c checks it). Returns:
 * - OVD_ERROR_NOT_A_TERMINAL if stdout is not a terminal, or input is on
 *   and stdin is not (002 R115), with nothing written or changed;
 * - OVD_ERROR_PLATFORM if a mode cannot be changed or the size cannot be
 *   read or reads as 0 (R25), with every completed step undone.
 * On OVD_OK, *system records the changed modes, alternate_screen, and the
 * reports. */
[[nodiscard]] OvdResult ovd_terminal_system_open(OvdTerminalSystem *system,
                                                 const OvdTerminalConfig *config, OvdSize *size);

/* The color depth the environment asks for (detect.h), with the Windows
 * virtual-terminal flag taken from what open recorded. */
[[nodiscard]] OvdColorDepth ovd_terminal_system_detect(const OvdTerminalSystem *system);

/* Undoes R28 steps 4, 3, 2, writing nothing: create's rollback when a
 * step after open fails (allocation, or enter after its blocks are
 * freed). */
void ovd_terminal_system_undo(OvdTerminalSystem *system);

/* R28 step 7: writes the enter sequence in one write. On failure, part of
 * it may have reached the terminal, so the exit sequence is written
 * best-effort, leaving the alternate screen whenever it was requested and
 * moving to the last of `rows` otherwise, and the result is
 * OVD_ERROR_IO. The modes stay recorded, so the caller can free step 6's
 * blocks and then call undo: every step undone in reverse order. rows is
 * the size read at create. */
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

/* Input on the system backend (002 R106, R107, R109, R111): platform.h's
 * ovd_platform_input_wait, ovd_platform_input_read, and
 * ovd_platform_clock_ms on the modes create recorded. */
[[nodiscard]] OvdResult ovd_terminal_system_wait(OvdTerminalSystem *system, int32_t timeout_ms,
                                                 uint32_t *ready);
[[nodiscard]] OvdResult ovd_terminal_system_read(OvdTerminalSystem *system, char *bytes,
                                                 size_t capacity, size_t *length, bool *resized);
[[nodiscard]] uint64_t ovd_terminal_system_clock(void);

/* ---- Input (input.c) -------------------------------------------------- */

/* The fixed input queue (R110) and the paste chunk buffer: the worst-case
 * repair of a full queue, 3 bytes of U+FFFD per byte, plus the bytes the
 * decoder can hold (data-model, Input buffers; design D5). */
constexpr size_t OVD_INPUT_QUEUE_SIZE = 4096;
constexpr size_t OVD_INPUT_TEXT_SIZE = 3 * OVD_INPUT_QUEUE_SIZE + OVD_DECODER_TEXT_MIN;
static_assert(OVD_INPUT_TEXT_SIZE >= OVD_DECODER_TEXT_MIN, "the decoder's chunk minimum");

/* A terminal's input state. Owned by the terminal module; input.c reads
 * and writes the fields, terminal.c only adopts and releases the blocks.
 *
 * Invariants:
 * - queue.size == OVD_INPUT_QUEUE_SIZE and text.size == OVD_INPUT_TEXT_SIZE;
 * - start + length <= queue.size; the bytes not yet decoded are
 *   queue[start .. start + length), in the order they were fed;
 * - decoder's chunk buffer is text;
 * - clock_ms never decreases: headless virtual time (R109), or on the
 *   system backend the platform clock when the bytes were read;
 * - resize_unreported is set by ovd_terminal_set_size (headless) or by a
 *   wakeup whose re-read size differs from reported (system, R111), and
 *   cleared only by the wait that reports it;
 * - reported is, on the system backend, the size read at create or the
 *   last size a resize event reported. */
typedef struct OvdInput {
    OvdBlock queue;
    size_t start;
    size_t length;
    OvdBlock text;
    OvdDecoder decoder;
    uint64_t clock_ms;
    bool resize_unreported;
    OvdSize reported;
} OvdInput;

/* Takes queue and text, allocated with the sizes above, and starts the
 * decoder with the escape delay. Returns false only if text is smaller
 * than the decoder needs, which the sizes above rule out; the blocks are
 * then still recorded, so ovd_input_release frees them. */
[[nodiscard]] bool ovd_input_adopt(OvdInput *input, OvdBlock queue, OvdBlock text,
                                   uint32_t escape_delay_ms);

/* Releases both blocks; a zero-initialized OvdInput releases nothing. */
void ovd_input_release(OvdInput *input, const OvdAllocator *allocator);

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
 *   create changed until destroy restores it;
 * - input_on is the config's input, and input holds its blocks exactly
 *   when input_on (it is zero-initialized otherwise). */
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

    bool input_on;
    OvdInput input;
};

#endif /* OVERDRIVE_SRC_INTERNAL_H */
