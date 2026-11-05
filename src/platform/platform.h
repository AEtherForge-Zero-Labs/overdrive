/*
 * The operating-system layer under the system backend (FR-010, FR-011;
 * research R11, R25-R28; feature 002: R106, R107, R109, R111).
 *
 * Internal to Overdrive: nothing here is exported. One implementation is
 * compiled per build, platform_posix.c or platform_windows.c, chosen by
 * CMake. Those two files are the only ones that include OS headers, and
 * this header names no OS type. Plain functions, no function-pointer
 * table: there is one platform per build.
 *
 * Every function works on the process's standard handles: stdout for
 * output and size, stdin for input modes and input. Nothing here
 * allocates. What ovd_platform_enter changes is recorded in the caller's
 * OvdPlatformModes; the one exception is POSIX's resize wakeup (step 4c),
 * whose pipe and previous SIGWINCH action are process-wide, as a signal
 * handler needs them (spec FR-036, research R111).
 */
#ifndef OVERDRIVE_SRC_PLATFORM_PLATFORM_H
#define OVERDRIVE_SRC_PLATFORM_PLATFORM_H

#include <overdrive/core.h>

#include <stddef.h>
#include <stdint.h>

/* Bytes reserved in OvdPlatformModes for the values read before a change.
 * Each platform file checks with static_assert that what it saves fits. */
#define OVD_PLATFORM_SAVED_SIZE ((size_t)256)

/* What ovd_platform_enter changed, with the values read before each
 * change, so ovd_platform_leave can restore them (R26). Owned by the
 * caller, read and written only by the platform file: `steps` is the set
 * of completed steps and `saved` holds the platform's recorded values.
 * Zero-initialized means nothing is recorded. */
typedef struct OvdPlatformModes {
    uint32_t steps;
    alignas(max_align_t) unsigned char saved[OVD_PLATFORM_SAVED_SIZE];
} OvdPlatformModes;

/* R28 step 1: true if stdout is a terminal. POSIX: isatty. Windows: the
 * standard output handle is a console screen buffer. */
[[nodiscard]] bool ovd_platform_output_is_terminal(void);

/* R28 steps 2-4, in order, each reading the mode before changing it:
 * 2. (Windows) output mode: add virtual-terminal processing,
 *    DISABLE_NEWLINE_AUTO_RETURN, and processed output;
 * 3. (Windows) output code page: 65001 (UTF-8);
 * 4. input echo off (POSIX ECHO, Windows ENABLE_ECHO_INPUT), only if stdin
 *    is a terminal; otherwise the step is skipped, which is not an error
 *    (R27).
 * With input, then (002 R106, R107, R111):
 * 4b. raw input. POSIX: ICANON, ISIG, IEXTEN, IXON, ICRNL, INLCR, IGNCR,
 *     ISTRIP clear, VMIN and VTIME 0. Windows: line, echo, and processed
 *     input off, virtual-terminal and window input on, and with mouse,
 *     quick edit off;
 * 4c. (POSIX) the resize wakeup: a non-blocking, close-on-exec self-pipe
 *     and a SIGWINCH handler that writes to it and calls the previous
 *     handler. Installed by the first system terminal with input on and
 *     shared by later ones.
 * With input, stdin must be a terminal: OVD_ERROR_NOT_A_TERMINAL
 * otherwise, before anything changes. On OVD_OK, *modes records every
 * completed step. On failure the completed steps are undone in reverse,
 * *modes records nothing, and the result is OVD_ERROR_PLATFORM. Returns
 * OVD_ERROR_INVALID_ARGUMENT, changing nothing, if modes is nullptr or
 * already records a step: entering again would lose the values to
 * restore. mouse is ignored without input. */
[[nodiscard]] OvdResult ovd_platform_enter(OvdPlatformModes *modes, bool input, bool mouse);

/* Undoes the steps *modes records, in reverse order (4c, 4b, 4, 3, 2),
 * restoring each mode to the value read before it changed. 4c puts the
 * previous SIGWINCH action back, only if the current one is still
 * Overdrive's and this is its last user, then closes the pipe. Every step
 * is attempted even if an earlier one fails; failures are not reported
 * (R28). Then *modes records nothing, so a second call does nothing.
 * nullptr does nothing. */
void ovd_platform_leave(OvdPlatformModes *modes);

/* True if stdin is a terminal (POSIX isatty; Windows a console input
 * buffer). */
[[nodiscard]] bool ovd_platform_input_is_terminal(void);

/* What ovd_platform_input_wait found ready. */
typedef enum OvdPlatformReady : uint32_t {
    OVD_PLATFORM_READY_INPUT = 1u << 0,  /* stdin has input to read */
    OVD_PLATFORM_READY_RESIZE = 1u << 1, /* (POSIX) a SIGWINCH arrived */
} OvdPlatformReady;

/* Waits at most timeout_ms milliseconds, or without limit if it is
 * negative, until stdin has input or, on POSIX, a resize wakeup arrives,
 * and stores what is ready in *ready, a set of OvdPlatformReady: 0 when
 * the time passed or another signal interrupted the wait. A resize wakeup
 * is drained here, so several arrive as one. Requires *modes to record
 * step 4b. Returns OVD_ERROR_INVALID_ARGUMENT for nullptr or without 4b,
 * and OVD_ERROR_PLATFORM if the wait fails or stdin hung up. */
[[nodiscard]] OvdResult ovd_platform_input_wait(OvdPlatformModes *modes, int32_t timeout_ms,
                                                uint32_t *ready);

/* The smallest capacity ovd_platform_input_read takes: one UTF-16 unit
 * converted on Windows, with a pending unpaired surrogate repaired,
 * needs 6 bytes. */
#define OVD_PLATFORM_READ_MIN ((size_t)16)

/* Reads what stdin holds now, without waiting, into bytes[0 .. capacity),
 * storing the count in *length (0 if nothing is there). Windows reads
 * console input records: key-down characters as UTF-8 (a surrogate pair
 * may be split across calls; an unpaired surrogate becomes U+FFFD), and a
 * buffer-size record sets *resized. On POSIX *resized is false. Requires
 * step 4b and capacity >= OVD_PLATFORM_READ_MIN: OVD_ERROR_INVALID_ARGUMENT
 * otherwise, or for nullptr. OVD_ERROR_PLATFORM if the read fails, or on
 * POSIX if a wait saw stdin hang up and nothing is left to read: the bytes
 * queued before the hang-up are returned first. */
[[nodiscard]] OvdResult ovd_platform_input_read(OvdPlatformModes *modes, char *bytes,
                                                size_t capacity, size_t *length, bool *resized);

/* A monotonic clock in milliseconds, from an unspecified start. POSIX:
 * clock_gettime(CLOCK_MONOTONIC); Windows: GetTickCount64, whose
 * resolution is the system timer's, typically 10 to 16 ms. */
[[nodiscard]] uint64_t ovd_platform_clock_ms(void);

/* ---- Abnormal-exit restoration (002 R112, R113, FR-030 to FR-036) ---- */

/* The largest exit sequence the record holds (CAN first). terminal_system.c
 * checks that its own fit. */
#define OVD_PLATFORM_EXIT_MAX ((size_t)96)

/* One process-wide restoration record, owned by the first system terminal
 * with restore_on_exit that reserves it (data-model, Restoration record).
 * Its state machine is IDLE -> ARMED -> CLAIMED -> DONE -> IDLE: the one
 * that moves ARMED to CLAIMED, a handler or destroy, restores, so
 * restoration runs at most once (FR-033). The calls below are made by the
 * thread creating or destroying the owning terminal, in this order:
 * reserve, then arm or release; claim, then disarm.
 *
 * Takes the record if no terminal owns it, storing true in *owner, or
 * stores false and changes nothing. On POSIX the owner's calling thread
 * then has the handled signals blocked until arm or release, so one that
 * arrives meanwhile is delivered once the record is armed (R112).
 * OVD_ERROR_PLATFORM, taking nothing, if the mask cannot be set;
 * OVD_ERROR_INVALID_ARGUMENT for nullptr. */
[[nodiscard]] OvdResult ovd_platform_restore_reserve(bool *owner);

/* Create's last step, for the owner: records what to restore (the modes
 * *modes recorded as read before they changed, and the exit sequence,
 * truncated to OVD_PLATFORM_EXIT_MAX bytes), arms the record, installs the
 * handlers (POSIX: each FR-030 signal not ignored before create; Windows:
 * the console control handler, then the C runtime's after it, abort, and
 * the unhandled-exception filter), and unblocks. A handler that cannot be
 * installed is skipped. */
void ovd_platform_restore_arm(const OvdPlatformModes *modes, const char *exit, size_t length);

/* For the owner, when create fails after reserve and before arm: unblocks
 * and gives the record up. */
void ovd_platform_restore_release(void);

/* Destroy's first step, for the owner: blocks the handled signals (POSIX)
 * and takes the restore. True if the caller must now restore (write the
 * exit sequence and undo the modes); false if a handler restored already,
 * after waiting for it to finish. */
[[nodiscard]] bool ovd_platform_restore_claim(void);

/* Destroy's last step, for the owner, after the caller's restore: marks
 * the restore done if claimed, puts back each handler whose slot still
 * holds Overdrive's (one the application installed after create stays,
 * FR-032), returns the record to IDLE, unblocks, and gives it up. */
void ovd_platform_restore_disarm(bool claimed);

/* True if *modes records that Windows virtual-terminal output was
 * enabled; always false on POSIX and for nullptr. This is the "Windows
 * with VT enabled" input of R10 (detect.h). */
[[nodiscard]] bool ovd_platform_virtual_terminal(const OvdPlatformModes *modes);

/* Reads the visible size of the terminal on stdout into *size, in
 * columns and rows. Returns OVD_ERROR_PLATFORM, leaving *size unchanged,
 * if it cannot be read or either dimension is 0 (R25), and
 * OVD_ERROR_INVALID_ARGUMENT if size is nullptr. */
[[nodiscard]] OvdResult ovd_platform_size(OvdSize *size);

/* Writes all `length` bytes to stdout, continuing after partial writes
 * (and, on POSIX, after EINTR), with nothing else in between. Returns
 * true when every byte was accepted. Returns false if a write fails or
 * accepts nothing; an unknown prefix of the bytes may have been written.
 * length 0 returns true without writing; bytes may then be nullptr.
 * nullptr bytes with a non-zero length returns false without writing. */
[[nodiscard]] bool ovd_platform_write(const char *bytes, size_t length);

/* Returns the value of the environment variable `name`, or nullptr if it
 * is not set or name is nullptr (ISO C getenv; context is unused). The
 * string is valid until the environment is next changed. Matches
 * OvdEnvironmentLookup (detect.h), so it can be passed directly. */
[[nodiscard]] const char *ovd_platform_environment(void *context, const char *name);

#endif /* OVERDRIVE_SRC_PLATFORM_PLATFORM_H */
