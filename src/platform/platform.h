/*
 * The operating-system layer under the system backend (FR-010, FR-011;
 * research R11, R25-R28).
 *
 * Internal to Overdrive: nothing here is exported. One implementation is
 * compiled per build, platform_posix.c or platform_windows.c, chosen by
 * CMake. Those two files are the only ones that include OS headers, and
 * this header names no OS type. Plain functions, no function-pointer
 * table: there is one platform per build.
 *
 * Every function works on the process's standard handles: stdout for
 * output and size, stdin for input modes. Nothing here allocates or keeps
 * state between calls; what ovd_platform_enter changes is recorded in the
 * caller's OvdPlatformModes.
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
 * On OVD_OK, *modes records every completed step. On failure the
 * completed steps are undone in reverse, *modes records nothing, and the
 * result is OVD_ERROR_PLATFORM. Returns OVD_ERROR_INVALID_ARGUMENT,
 * changing nothing, if modes is nullptr or already records a step:
 * entering again would lose the values to restore. */
[[nodiscard]] OvdResult ovd_platform_enter(OvdPlatformModes *modes);

/* Undoes the steps *modes records, in reverse order (4, 3, 2), restoring
 * each mode to the value read before it changed. Every step is attempted
 * even if an earlier one fails; failures are not reported (R28). Then
 * *modes records nothing, so a second call does nothing. nullptr does
 * nothing. */
void ovd_platform_leave(OvdPlatformModes *modes);

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
