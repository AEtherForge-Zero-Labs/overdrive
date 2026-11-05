/*
 * platform.h on Windows 10 1809+ (research R11, R25-R28; feature 002
 * R107 raw input and input records, R109 clock).
 *
 * Each call is checked against, and cited from, Microsoft's reference:
 * - https://learn.microsoft.com/windows/console/<name> for GetStdHandle,
 *   GetConsoleMode, SetConsoleMode, GetConsoleOutputCP,
 *   SetConsoleOutputCP, GetConsoleScreenBufferInfo,
 *   CONSOLE_SCREEN_BUFFER_INFO (console-screen-buffer-info-str),
 *   SMALL_RECT (small-rect-str), ReadConsoleInput,
 *   GetNumberOfConsoleInputEvents, INPUT_RECORD (input-record-str), and
 *   KEY_EVENT_RECORD (key-event-record-str);
 * - https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-writefile
 *   for WriteFile;
 * - .../win32/api/synchapi/nf-synchapi-waitforsingleobject and
 *   .../win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64;
 * - ISO C for getenv.
 * Observed only under Wine, which is not Windows (baseline); behavior in
 * Windows Terminal and the console host is checked by hand (OVD-12, and
 * for input the 002 manual checklist).
 */
#define WIN32_LEAN_AND_MEAN

#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* The steps ovd_platform_enter can complete (R28 steps 2, 3, 4, and
 * 002's 4b). */
typedef enum PlatformStep : uint32_t {
    STEP_OUTPUT_MODE = 1u << 0,
    STEP_CODE_PAGE = 1u << 1,
    STEP_INPUT_ECHO = 1u << 2,
    STEP_RAW_INPUT = 1u << 3,
} PlatformStep;

/* What OvdPlatformModes.saved holds on Windows: each value as read
 * before it changed (R26), and the read state input records need. */
typedef struct Saved {
    DWORD output_mode;
    UINT code_page;
    DWORD input_mode;
    /* the input mode read before raw input, that is, after echo (R107) */
    DWORD raw_mode;
    /* a high surrogate whose low half has not been read yet, or 0 */
    WCHAR high_surrogate;
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

bool ovd_platform_input_is_terminal(void)
{
    DWORD mode = 0;
    return console_mode(standard_handle(STD_INPUT_HANDLE), &mode);
}

/* Restores the recorded steps in reverse order (4b, 4, 3, 2), attempting
 * every one even if an earlier one fails (R28). */
static void restore(const Saved *saved, uint32_t steps)
{
    if ((steps & STEP_RAW_INPUT) != 0) {
        HANDLE input = standard_handle(STD_INPUT_HANDLE);
        if (input != nullptr) {
            SetConsoleMode(input, saved->raw_mode);
        }
    }
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

OvdResult ovd_platform_enter(OvdPlatformModes *modes, bool input_on, bool mouse)
{
    if (modes == nullptr || modes->steps != 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    /* With input, stdin must be a console, before anything changes
     * (002 R115). */
    if (input_on && !ovd_platform_input_is_terminal()) {
        return OVD_ERROR_NOT_A_TERMINAL;
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

    /* Step 4b (R107). While ENABLE_PROCESSED_INPUT is set, "CTRL+C is
     * processed by the system and is not placed in the input buffer";
     * clearing it makes Ctrl+C a key (FR-002), and line and echo input
     * go too. ENABLE_VIRTUAL_TERMINAL_INPUT "directs the Virtual Terminal
     * processing engine to convert user input ... into Console Virtual
     * Terminal Sequences"; ENABLE_WINDOW_INPUT reports buffer-size changes
     * "in the console's input buffer". Quick edit: "To disable this mode,
     * use ENABLE_EXTENDED_FLAGS without this flag." */
    if (input_on) {
        if (!console_mode(input, &saved.raw_mode)) {
            restore(&saved, steps);
            return OVD_ERROR_PLATFORM;
        }
        DWORD raw = saved.raw_mode
                    & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        raw |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT;
        if (mouse) {
            raw = (raw | ENABLE_EXTENDED_FLAGS) & ~(DWORD)ENABLE_QUICK_EDIT_MODE;
        }
        if (SetConsoleMode(input, raw) == 0) {
            restore(&saved, steps);
            return OVD_ERROR_PLATFORM;
        }
        steps |= STEP_RAW_INPUT;
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

/* ---- Input (002 R107, R109) ------------------------------------------ */

OvdResult ovd_platform_input_wait(OvdPlatformModes *modes, int32_t timeout_ms, uint32_t *ready)
{
    if (modes == nullptr || ready == nullptr || (modes->steps & STEP_RAW_INPUT) == 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    *ready = 0;
    HANDLE input = standard_handle(STD_INPUT_HANDLE);
    if (input == nullptr) {
        return OVD_ERROR_PLATFORM;
    }
    /* WaitForSingleObject can wait for "Console input"; WAIT_OBJECT_0:
     * "The state of the specified object is signaled", WAIT_TIMEOUT: "The
     * time-out interval elapsed". That the input handle is signaled while
     * unread records remain is ASSUMED (R107). */
    const DWORD wait = timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms;
    switch (WaitForSingleObject(input, wait)) {
    case WAIT_OBJECT_0:
        *ready = OVD_PLATFORM_READY_INPUT;
        return OVD_OK;
    case WAIT_TIMEOUT:
        return OVD_OK;
    default:
        return OVD_ERROR_PLATFORM;
    }
}

/* Appends code_point (a scalar value) as UTF-8: at most 4 bytes. */
static size_t put_utf8(char *out, uint32_t code_point)
{
    if (code_point < 0x80) {
        out[0] = (char)code_point;
        return 1;
    }
    if (code_point < 0x800) {
        out[0] = (char)(0xC0 | (code_point >> 6));
        out[1] = (char)(0x80 | (code_point & 0x3F));
        return 2;
    }
    if (code_point < 0x10000) {
        out[0] = (char)(0xE0 | (code_point >> 12));
        out[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code_point & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (code_point >> 18));
    out[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
    out[3] = (char)(0x80 | (code_point & 0x3F));
    return 4;
}

static constexpr uint32_t REPLACEMENT = 0xFFFD;

/* Converts one UTF-16 unit, pairing surrogates across calls through
 * saved->high_surrogate: at most 6 bytes (a repaired unpaired high
 * surrogate, then a 3-byte unit). An unpaired surrogate becomes U+FFFD,
 * as the decoder repairs ill-formed UTF-8 (FR-019). */
static size_t put_unit(Saved *saved, WCHAR unit, char *out)
{
    size_t length = 0;
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
        if (saved->high_surrogate != 0) {
            const uint32_t code_point = 0x10000 + (((uint32_t)saved->high_surrogate - 0xD800) << 10)
                                        + ((uint32_t)unit - 0xDC00);
            saved->high_surrogate = 0;
            return put_utf8(out, code_point);
        }
        return put_utf8(out, REPLACEMENT);
    }
    if (saved->high_surrogate != 0) {
        length = put_utf8(out, REPLACEMENT);
        saved->high_surrogate = 0;
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
        saved->high_surrogate = unit;
        return length;
    }
    return length + put_utf8(out + length, unit);
}

/* The most input records one read takes: a bound on the stack array. */
static constexpr DWORD RECORDS_MAX = 64;
/* Bytes one UTF-16 unit can produce (put_unit). */
static constexpr size_t UNIT_BYTES_MAX = 6;

OvdResult ovd_platform_input_read(OvdPlatformModes *modes, char *bytes, size_t capacity,
                                  size_t *length, bool *resized)
{
    if (modes == nullptr || bytes == nullptr || length == nullptr || resized == nullptr
        || capacity < OVD_PLATFORM_READ_MIN || (modes->steps & STEP_RAW_INPUT) == 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    *length = 0;
    *resized = false;
    HANDLE input = standard_handle(STD_INPUT_HANDLE);
    if (input == nullptr) {
        return OVD_ERROR_PLATFORM;
    }
    /* GetNumberOfConsoleInputEvents "Retrieves the number of unread input
     * records". ReadConsoleInput "does not return until at least one input
     * record has been read", so it is called only when some are unread. */
    DWORD unread = 0;
    if (GetNumberOfConsoleInputEvents(input, &unread) == 0) {
        return OVD_ERROR_PLATFORM;
    }
    if (unread == 0) {
        return OVD_OK;
    }
    /* Each record can produce at least one unit; take no more records than
     * the capacity has units for. The loop below keeps room for one unit
     * of every later record, so a repeat count never crowds one out. */
    DWORD want = unread < RECORDS_MAX ? unread : RECORDS_MAX;
    const size_t fit = capacity / UNIT_BYTES_MAX;
    if (want > fit) {
        want = (DWORD)fit;
    }
    INPUT_RECORD records[RECORDS_MAX];
    DWORD got = 0;
    if (ReadConsoleInputW(input, records, want, &got) == 0) {
        return OVD_ERROR_PLATFORM;
    }
    Saved saved;
    memcpy(&saved, modes->saved, sizeof saved);
    size_t used = 0;
    for (DWORD i = 0; i < got && i < want; ++i) {
        const INPUT_RECORD *record = &records[i];
        if (record->EventType == WINDOW_BUFFER_SIZE_EVENT) {
            *resized = true;
            continue;
        }
        /* KEY_EVENT_RECORD: bKeyDown "If the key is pressed, this member
         * is TRUE"; UnicodeChar "Translated Unicode character". With
         * virtual-terminal input these carry the sequence's units
         * (ASSUMED, R107). wRepeatCount repeats a held key; repeats past
         * the room left after one unit for each later record are dropped,
         * and a surrogate half is taken once, since repeating one half
         * cannot form pairs. Other records are not input. */
        if (record->EventType != KEY_EVENT || !record->Event.KeyEvent.bKeyDown) {
            continue;
        }
        const WCHAR unit = record->Event.KeyEvent.uChar.UnicodeChar;
        if (unit == 0) {
            continue;
        }
        WORD repeat = record->Event.KeyEvent.wRepeatCount;
        if (repeat == 0 || (unit >= 0xD800 && unit <= 0xDFFF)) {
            repeat = 1;
        }
        /* This record and each later one keep one unit's room. */
        const size_t reserved = (size_t)(got - i) * UNIT_BYTES_MAX;
        used += put_unit(&saved, unit, bytes + used);
        for (WORD r = 1; r < repeat && capacity - used >= reserved; ++r) {
            used += put_unit(&saved, unit, bytes + used);
        }
    }
    memcpy(modes->saved, &saved, sizeof saved);
    *length = used;
    return OVD_OK;
}

uint64_t ovd_platform_clock_ms(void)
{
    /* GetTickCount64 "Retrieves the number of milliseconds that have
     * elapsed since the system was started"; its resolution is "typically
     * in the range of 10 milliseconds to 16 milliseconds". */
    return (uint64_t)GetTickCount64();
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
