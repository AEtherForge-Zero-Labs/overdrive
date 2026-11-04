/*
 * platform.h on POSIX systems (Linux, macOS): research R11, R25-R28.
 *
 * Each call is checked against, and cited from:
 * - POSIX.1-2024 (IEEE Std 1003.1-2024),
 *   https://pubs.opengroup.org/onlinepubs/9799919799/functions/<name>.html
 *   for isatty, tcgetattr, tcsetattr, and write;
 * - ISO C for getenv;
 * - TIOCGWINSZ, which is not in POSIX: the Linux man-pages project
 *   (https://man7.org/linux/man-pages/man2/TIOCGWINSZ.2const.html) and the
 *   macOS SDK's <sys/ttycom.h>. POSIX Issue 8's tcgetwinsize would be the
 *   standard form, but the macOS SDK does not declare it.
 */
#define _POSIX_C_SOURCE 200809L

#include "platform.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* The steps ovd_platform_enter can complete. POSIX has only R28 step 4. */
typedef enum PlatformStep : uint32_t {
    STEP_INPUT_ECHO = 1u << 0,
} PlatformStep;

/* What OvdPlatformModes.saved holds on POSIX. */
typedef struct Saved {
    /* stdin's termios as read before echo was turned off (R26). */
    struct termios input;
} Saved;

static_assert(sizeof(Saved) <= OVD_PLATFORM_SAVED_SIZE,
              "the saved termios must fit OvdPlatformModes.saved");

bool ovd_platform_output_is_terminal(void)
{
    /* isatty: "shall return 1 if fildes is associated with a terminal;
     * otherwise, it shall return 0". */
    return isatty(STDOUT_FILENO) == 1;
}

/* Sets stdin's termios immediately (TCSANOW: pending input is the user's,
 * and nothing here reads it, so it is not flushed). tcsetattr lists EINTR,
 * "A signal interrupted tcsetattr()", so that error is retried. */
static bool set_input_modes(const struct termios *modes)
{
    int result = 0;
    do {
        result = tcsetattr(STDIN_FILENO, TCSANOW, modes);
    } while (result != 0 && errno == EINTR);
    return result == 0;
}

OvdResult ovd_platform_enter(OvdPlatformModes *modes)
{
    if (modes == nullptr || modes->steps != 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }

    /* R27: input modes change only if stdin is a terminal; otherwise the
     * step is skipped, and that is not an error. */
    if (isatty(STDIN_FILENO) != 1) {
        return OVD_OK;
    }

    Saved saved = {};
    if (tcgetattr(STDIN_FILENO, &saved.input) != 0) {
        return OVD_ERROR_PLATFORM;
    }
    struct termios changed = saved.input;
    changed.c_lflag &= ~(tcflag_t)ECHO;
    /* tcsetattr fails only when it performed none of the request. */
    if (!set_input_modes(&changed)) {
        return OVD_ERROR_PLATFORM;
    }
    /* tcsetattr "shall return successfully if it was able to perform any
     * of the requested actions", so success does not prove ECHO is off:
     * read it back, and restore the recorded modes if it is not. */
    struct termios check = {};
    if (tcgetattr(STDIN_FILENO, &check) != 0 || (check.c_lflag & ECHO) != 0) {
        set_input_modes(&saved.input);
        return OVD_ERROR_PLATFORM;
    }

    memcpy(modes->saved, &saved, sizeof saved);
    modes->steps = STEP_INPUT_ECHO;
    return OVD_OK;
}

void ovd_platform_leave(OvdPlatformModes *modes)
{
    if (modes == nullptr) {
        return;
    }
    if ((modes->steps & STEP_INPUT_ECHO) != 0) {
        Saved saved;
        memcpy(&saved, modes->saved, sizeof saved);
        /* Best effort (R28): a failure here cannot be reported. */
        set_input_modes(&saved.input);
    }
    *modes = (OvdPlatformModes){};
}

/* POSIX output needs no mode for virtual-terminal sequences, so R10's
 * Windows rule never applies. The parameter is unnamed: it is unused. */
bool ovd_platform_virtual_terminal(const OvdPlatformModes *)
{
    return false;
}

OvdResult ovd_platform_size(OvdSize *size)
{
    if (size == nullptr) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    /* TIOCGWINSZ: "Get window size", into a struct winsize whose ws_row
     * and ws_col are unsigned short. On error -1 is returned. */
    struct winsize window = {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) != 0) {
        return OVD_ERROR_PLATFORM;
    }
    /* R25: a size that reads as 0 in either dimension is a failure. */
    if (window.ws_col == 0 || window.ws_row == 0) {
        return OVD_ERROR_PLATFORM;
    }
    *size = (OvdSize){ .cols = window.ws_col, .rows = window.ws_row };
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
    size_t done = 0;
    while (done < length) {
        /* Above SSIZE_MAX "the result is implementation-defined". */
        size_t chunk = length - done;
        if (chunk > (size_t)SSIZE_MAX) {
            chunk = (size_t)SSIZE_MAX;
        }
        const ssize_t written = write(STDOUT_FILENO, bytes + done, chunk);
        if (written < 0) {
            /* "If write() is interrupted by a signal before it writes any
             * data, it shall return -1 with errno set to [EINTR]." */
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        /* POSIX defines no zero return for a non-zero request; treating
         * it as a failure keeps the loop from spinning. */
        if (written == 0) {
            return false;
        }
        /* "If write() is interrupted by a signal after it successfully
         * writes some data, it shall return the number of bytes written":
         * continue after them. */
        done += (size_t)written;
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
