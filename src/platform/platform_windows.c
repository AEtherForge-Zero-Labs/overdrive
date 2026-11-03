/*
 * platform.h on Windows 10 1809+ (research R11, R25-R28).
 *
 * Each call is checked against, and cited from, Microsoft's reference:
 * - https://learn.microsoft.com/windows/console/<name> for GetStdHandle,
 *   GetConsoleMode, SetConsoleMode, GetConsoleOutputCP,
 *   SetConsoleOutputCP, GetConsoleScreenBufferInfo,
 *   CONSOLE_SCREEN_BUFFER_INFO (console-screen-buffer-info-str), and
 *   SMALL_RECT (small-rect-str);
 * - https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-writefile
 *   for WriteFile;
 * - ISO C for getenv.
 * Observed only under Wine, which is not Windows (baseline); behavior in
 * Windows Terminal and the console host is checked by hand (OVD-12).
 */
#define WIN32_LEAN_AND_MEAN

#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* The steps ovd_platform_enter can complete (R28 steps 2, 3, 4). */
typedef enum PlatformStep : uint32_t {
    STEP_OUTPUT_MODE = 1u << 0,
    STEP_CODE_PAGE = 1u << 1,
    STEP_INPUT_ECHO = 1u << 2,
} PlatformStep;

/* What OvdPlatformModes.saved holds on Windows: each value as read
 * before it changed (R26). */
typedef struct Saved {
    DWORD output_mode;
    UINT code_page;
    DWORD input_mode;
} Saved;

static_assert(sizeof(Saved) <= OVD_PLATFORM_SAVED_SIZE,
              "the saved console modes must fit OvdPlatformModes.saved");

/* A standard handle, or nullptr. GetStdHandle returns INVALID_HANDLE_VALUE
 * on failure, and NULL "if an application does not have associated
 * standard handles". */
static HANDLE standard_handle(DWORD which)
{
    HANDLE handle = GetStdHandle(which);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

/* True if handle is a console buffer, storing its mode. GetConsoleMode
 * takes "a handle to the console input buffer or the console screen
 * buffer"; that it fails (returns zero) for a redirected file or pipe is
 * INFERRED from that requirement, not stated. */
static bool console_mode(HANDLE handle, DWORD *mode)
{
    return handle != nullptr && GetConsoleMode(handle, mode) != 0;
}

bool ovd_platform_output_is_terminal(void)
{
    DWORD mode = 0;
    return console_mode(standard_handle(STD_OUTPUT_HANDLE), &mode);
}

/* Restores the recorded steps in reverse order (4, 3, 2), attempting
 * every one even if an earlier one fails (R28). */
static void restore(const Saved *saved, uint32_t steps)
{
    if ((steps & STEP_INPUT_ECHO) != 0) {
        HANDLE input = standard_handle(STD_INPUT_HANDLE);
        if (input != nullptr) {
            SetConsoleMode(input, saved->input_mode);
        }
    }
    if ((steps & STEP_CODE_PAGE) != 0) {
        SetConsoleOutputCP(saved->code_page);
    }
    if ((steps & STEP_OUTPUT_MODE) != 0) {
        HANDLE output = standard_handle(STD_OUTPUT_HANDLE);
        if (output != nullptr) {
            SetConsoleMode(output, saved->output_mode);
        }
    }
}

OvdResult ovd_platform_enter(OvdPlatformModes *modes)
{
    if (modes == nullptr || modes->steps != 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    Saved saved = {};
    uint32_t steps = 0;

    /* Step 2. ENABLE_VIRTUAL_TERMINAL_PROCESSING parses VT sequences
     * written with WriteFile; DISABLE_NEWLINE_AUTO_RETURN defers the wrap
     * at the last column (R33). SetConsoleMode's reference says, at the
     * former, "Ensure ENABLE_PROCESSED_OUTPUT is set when using this
     * flag", so it is set too (R40). A console without VT support refuses
     * the mode: there is no pre-1809 fallback (R28). */
    HANDLE output = standard_handle(STD_OUTPUT_HANDLE);
    if (!console_mode(output, &saved.output_mode)) {
        return OVD_ERROR_PLATFORM;
    }
    const DWORD output_mode = saved.output_mode | ENABLE_PROCESSED_OUTPUT
                              | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    if (SetConsoleMode(output, output_mode) == 0) {
        return OVD_ERROR_PLATFORM;
    }
    steps |= STEP_OUTPUT_MODE;

    /* Step 3. GetConsoleOutputCP: "If the return value is zero, the
     * function has failed." */
    saved.code_page = GetConsoleOutputCP();
    if (saved.code_page == 0 || SetConsoleOutputCP(CP_UTF8) == 0) {
        restore(&saved, steps);
        return OVD_ERROR_PLATFORM;
    }
    steps |= STEP_CODE_PAGE;

    /* Step 4, only if stdin is a console (R27). ENABLE_ECHO_INPUT "can be
     * used only if the ENABLE_LINE_INPUT mode is also enabled"; clearing
     * echo alone leaves line input as it was, which is allowed. */
    HANDLE input = standard_handle(STD_INPUT_HANDLE);
    if (console_mode(input, &saved.input_mode)) {
        if (SetConsoleMode(input, saved.input_mode & ~(DWORD)ENABLE_ECHO_INPUT) == 0) {
            restore(&saved, steps);
            return OVD_ERROR_PLATFORM;
        }
        steps |= STEP_INPUT_ECHO;
    }

    memcpy(modes->saved, &saved, sizeof saved);
    modes->steps = steps;
    return OVD_OK;
}

void ovd_platform_leave(OvdPlatformModes *modes)
{
    if (modes == nullptr) {
        return;
    }
    if (modes->steps != 0) {
        Saved saved;
        memcpy(&saved, modes->saved, sizeof saved);
        restore(&saved, modes->steps);
    }
    *modes = (OvdPlatformModes){};
}

bool ovd_platform_virtual_terminal(const OvdPlatformModes *modes)
{
    return modes != nullptr && (modes->steps & STEP_OUTPUT_MODE) != 0;
}

OvdResult ovd_platform_size(OvdSize *size)
{
    if (size == nullptr) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    /* srWindow holds "the console screen buffer coordinates of the
     * upper-left and lower-right corners of the display window", in
     * character cells. That both corners are inside the window, so each
     * dimension is the difference plus one, is INFERRED from SMALL_RECT's
     * "coordinates specify the rows and columns of screen-buffer
     * character cells". */
    HANDLE output = standard_handle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (output == nullptr || GetConsoleScreenBufferInfo(output, &info) == 0) {
        return OVD_ERROR_PLATFORM;
    }
    const int32_t cols = (int32_t)info.srWindow.Right - (int32_t)info.srWindow.Left + 1;
    const int32_t rows = (int32_t)info.srWindow.Bottom - (int32_t)info.srWindow.Top + 1;
    /* R25: a size that reads as 0 in either dimension is a failure. */
    if (cols <= 0 || rows <= 0) {
        return OVD_ERROR_PLATFORM;
    }
    *size = (OvdSize){ .cols = cols, .rows = rows };
    return OVD_OK;
}

bool ovd_platform_write(const char *bytes, size_t length)
{
    if (length == 0) {
        return true;
    }
    if (bytes == nullptr) {
        return false;
    }
    HANDLE output = standard_handle(STD_OUTPUT_HANDLE);
    if (output == nullptr) {
        return false;
    }
    size_t done = 0;
    while (done < length) {
        /* nNumberOfBytesToWrite is a DWORD. */
        const size_t remaining = length - done;
        const DWORD chunk = remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
        DWORD written = 0;
        /* On a synchronous handle WriteFile returns once the request is
         * written or fails, but a non-blocking byte-mode pipe "returns
         * TRUE with *lpNumberOfBytesWritten < nNumberOfBytesToWrite", so
         * the loop continues after a short count. Zero bytes for a
         * non-zero request is a failure, so the loop cannot spin. */
        if (WriteFile(output, bytes + done, chunk, &written, nullptr) == 0 || written == 0) {
            return false;
        }
        done += written;
    }
    return true;
}

/* The context is unnamed: it is unused (OvdEnvironmentLookup's shape). */
const char *ovd_platform_environment(void *, const char *name)
{
    if (name == nullptr) {
        return nullptr;
    }
    return getenv(name);
}
