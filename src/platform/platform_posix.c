/*
 * platform.h on POSIX systems (Linux, macOS): research R11, R25-R28;
 * feature 002 R106 (raw input), R109 (clock), R111 (resize wakeup).
 *
 * Each call is checked against, and cited from:
 * - POSIX.1-2024 (IEEE Std 1003.1-2024),
 *   https://pubs.opengroup.org/onlinepubs/9799919799/functions/<name>.html
 *   for isatty, tcgetattr, tcsetattr, write, read, poll, pipe, fcntl,
 *   sigaction, and clock_gettime; XBD <termios.h> and XBD 11.1.7
 *   ("Non-Canonical Mode Input Processing") for the termios flags and
 *   VMIN/VTIME; XSH 2.4.3 for the async-signal-safe functions;
 * - ISO C for getenv;
 * - TIOCGWINSZ, which is not in POSIX: the Linux man-pages project
 *   (https://man7.org/linux/man-pages/man2/TIOCGWINSZ.2const.html) and the
 *   macOS SDK's <sys/ttycom.h>. POSIX Issue 8's tcgetwinsize would be the
 *   standard form, but the macOS SDK does not declare it.
 *
 * SIGWINCH is POSIX.1-2024, newer than the 2008 level requested below:
 * the macOS SDK's <sys/signal.h> defines it only without _POSIX_C_SOURCE
 * or with _DARWIN_C_SOURCE, and glibc exposes its non-2008 names with
 * _DEFAULT_SOURCE. Each macro does nothing on the other system.
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE

#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* The steps ovd_platform_enter can complete: R28 step 4, and with input
 * 002's 4b and 4c. */
typedef enum PlatformStep : uint32_t {
    STEP_INPUT_ECHO = 1u << 0,
    STEP_RAW_INPUT = 1u << 1,
    STEP_RESIZE_WAKE = 1u << 2,
} PlatformStep;

/* What OvdPlatformModes.saved holds on POSIX. */
typedef struct Saved {
    /* stdin's termios as read before echo was turned off (R26). */
    struct termios input;
    /* stdin's termios as read before raw mode, that is, after echo (R106). */
    struct termios raw;
    /* Set by a wait that saw stdin hang up with input still reported, so
     * the read that then finds nothing reports the hang-up. */
    bool hung_up;
} Saved;

static_assert(sizeof(Saved) <= OVD_PLATFORM_SAVED_SIZE,
              "the saved termios must fit OvdPlatformModes.saved");

bool ovd_platform_output_is_terminal(void)
{
    /* isatty: "shall return 1 if fildes is associated with a terminal;
     * otherwise, it shall return 0". */
    return isatty(STDOUT_FILENO) == 1;
}

bool ovd_platform_input_is_terminal(void)
{
    return isatty(STDIN_FILENO) == 1;
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

/* ---- Resize wakeup (step 4c, R111) ----------------------------------- */

/* Process-wide: the handler cannot reach a terminal (FR-036). Written only
 * by the thread creating or destroying a system terminal; the handler
 * reads wake_write and previous_winch. wake_write is -1 whenever no pipe
 * is open, so a handler run after the last destroy writes nothing. */
static volatile sig_atomic_t wake_write = -1;
static int wake_read = -1;
static struct sigaction previous_winch;
static size_t wake_users;

/* The SIGWINCH handler. write is async-signal-safe (XSH 2.4.3), and errno
 * is saved and restored, as 2.4.3 requires of a handler that may change
 * it. A full pipe drops the byte: one pending byte is enough. Then the
 * action the application had before create runs (FR-032). */
static void on_winch(int signal, siginfo_t *info, void *context)
{
    const int saved_errno = errno;
    const int fd = wake_write;
    if (fd >= 0) {
        const char byte = 0;
        const ssize_t written = write(fd, &byte, 1);
        if (written < 0) {
            /* EAGAIN: a wakeup is already pending. Nothing else to do. */
        }
    }
    if ((previous_winch.sa_flags & SA_SIGINFO) != 0) {
        if (previous_winch.sa_sigaction != nullptr) {
            previous_winch.sa_sigaction(signal, info, context);
        }
    } else if (previous_winch.sa_handler != SIG_DFL && previous_winch.sa_handler != SIG_IGN) {
        previous_winch.sa_handler(signal);
    }
    errno = saved_errno;
}

static bool is_ours(const struct sigaction *action)
{
    return (action->sa_flags & SA_SIGINFO) != 0 && action->sa_sigaction == on_winch;
}

/* Sets O_NONBLOCK (fcntl F_GETFL/F_SETFL) and FD_CLOEXEC (F_GETFD/F_SETFD)
 * on fd. pipe2 would set both atomically, but the macOS SDK does not
 * declare it (ASSUMED, as for ppoll in R111); the race it closes is a
 * concurrent exec in another thread, outside FR-036's thread rule. */
static bool set_pipe_flags(int fd)
{
    const int status = fcntl(fd, F_GETFL);
    const int descriptor = fcntl(fd, F_GETFD);
    return status != -1 && descriptor != -1 && fcntl(fd, F_SETFL, status | O_NONBLOCK) != -1
           && fcntl(fd, F_SETFD, descriptor | FD_CLOEXEC) != -1;
}

/* Opens the pipe and installs the handler for the first user; later users
 * share them. Reinstalling would make the handler chain to itself. */
static bool wake_open(void)
{
    if (wake_users > 0) {
        ++wake_users;
        return true;
    }
    int fds[2];
    if (pipe(fds) != 0) {
        return false;
    }
    if (!set_pipe_flags(fds[0]) || !set_pipe_flags(fds[1])) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    /* The previous action is read first, so a signal arriving the moment
     * the new one is installed finds it filled in. One that is already
     * Overdrive's (left installed by a failed restore) is not chained. */
    struct sigaction previous;
    if (sigaction(SIGWINCH, nullptr, &previous) != 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (is_ours(&previous)) {
        previous = (struct sigaction){ .sa_handler = SIG_DFL };
        sigemptyset(&previous.sa_mask);
    }
    previous_winch = previous;
    wake_read = fds[0];
    wake_write = fds[1];
    /* SA_RESTART: other calls in the application are not interrupted by
     * resizes (R111); SA_SIGINFO, so an SA_SIGINFO previous action gets
     * the arguments it expects. */
    struct sigaction action = { .sa_flags = SA_SIGINFO | SA_RESTART };
    action.sa_sigaction = on_winch;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGWINCH, &action, nullptr) != 0) {
        wake_write = -1;
        wake_read = -1;
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    wake_users = 1;
    return true;
}

/* The last user puts the previous action back, only if the current one is
 * still Overdrive's (FR-032: one the application installed after create
 * stays), then closes the pipe. */
static void wake_close(void)
{
    if (wake_users == 0 || --wake_users > 0) {
        return;
    }
    struct sigaction current;
    if (sigaction(SIGWINCH, nullptr, &current) == 0 && is_ours(&current)) {
        sigaction(SIGWINCH, &previous_winch, nullptr);
    }
    const int write_end = wake_write;
    wake_write = -1;
    close(write_end);
    close(wake_read);
    wake_read = -1;
}

/* Empties the pipe: several wakeups arrive as one (R111). read on an
 * O_NONBLOCK pipe with no data "shall return -1 and set errno to
 * [EAGAIN]", which ends the loop. */
static void wake_drain(void)
{
    char bytes[64];
    for (;;) {
        const ssize_t got = read(wake_read, bytes, sizeof bytes);
        if (got > 0) {
            continue;
        }
        if (got < 0 && errno == EINTR) {
            continue;
        }
        return;
    }
}

/* ---- Modes ----------------------------------------------------------- */

/* Undoes the steps in reverse: 4c, 4b, 4. Each is attempted even if an
 * earlier one fails (R28). */
static void restore(const Saved *saved, uint32_t steps)
{
    if ((steps & STEP_RESIZE_WAKE) != 0) {
        wake_close();
    }
    if ((steps & STEP_RAW_INPUT) != 0) {
        set_input_modes(&saved->raw);
    }
    if ((steps & STEP_INPUT_ECHO) != 0) {
        set_input_modes(&saved->input);
    }
}

/* R106's raw-mode changes to modes. */
static void make_raw(struct termios *modes)
{
    modes->c_lflag &= ~(tcflag_t)(ICANON | ISIG | IEXTEN);
    modes->c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR | IGNCR | ISTRIP);
    /* XBD 11.1.7, Case D, MIN=0, TIME=0: "If no characters are available,
     * read() shall return a value of zero". The wait is poll's. */
    modes->c_cc[VMIN] = 0;
    modes->c_cc[VTIME] = 0;
}

static bool is_raw(const struct termios *modes)
{
    return (modes->c_lflag & (ICANON | ISIG | IEXTEN | ECHO)) == 0
           && (modes->c_iflag & (IXON | ICRNL | INLCR | IGNCR | ISTRIP)) == 0
           && modes->c_cc[VMIN] == 0 && modes->c_cc[VTIME] == 0;
}

/* The mouse parameter is unnamed: it is unused. It turns off Windows'
 * quick edit; POSIX mouse reporting is the enter sequence's alone. */
OvdResult ovd_platform_enter(OvdPlatformModes *modes, bool input, bool)
{
    if (modes == nullptr || modes->steps != 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }

    /* R27: input modes change only if stdin is a terminal; otherwise the
     * step is skipped, and that is not an error. With input, a terminal
     * is required (002 R115). */
    if (isatty(STDIN_FILENO) != 1) {
        return input ? OVD_ERROR_NOT_A_TERMINAL : OVD_OK;
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
    uint32_t steps = STEP_INPUT_ECHO;

    if (input) {
        /* Step 4b (R106), read before change and read back, as step 4. */
        if (tcgetattr(STDIN_FILENO, &saved.raw) != 0) {
            restore(&saved, steps);
            return OVD_ERROR_PLATFORM;
        }
        changed = saved.raw;
        make_raw(&changed);
        if (!set_input_modes(&changed)) {
            restore(&saved, steps);
            return OVD_ERROR_PLATFORM;
        }
        steps |= STEP_RAW_INPUT;
        if (tcgetattr(STDIN_FILENO, &check) != 0 || !is_raw(&check)) {
            restore(&saved, steps);
            return OVD_ERROR_PLATFORM;
        }
        /* Step 4c (R111). */
        if (!wake_open()) {
            restore(&saved, steps);
            return OVD_ERROR_PLATFORM;
        }
        steps |= STEP_RESIZE_WAKE;
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
        /* Best effort (R28): a failure here cannot be reported. */
        restore(&saved, modes->steps);
    }
    *modes = (OvdPlatformModes){};
}

/* POSIX output needs no mode for virtual-terminal sequences, so R10's
 * Windows rule never applies. The parameter is unnamed: it is unused. */
bool ovd_platform_virtual_terminal(const OvdPlatformModes *)
{
    return false;
}

/* ---- Input (002 R106, R109, R111) ------------------------------------ */

OvdResult ovd_platform_input_wait(OvdPlatformModes *modes, int32_t timeout_ms, uint32_t *ready)
{
    if (modes == nullptr || ready == nullptr || (modes->steps & STEP_RAW_INPUT) == 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    *ready = 0;
    const bool wake = (modes->steps & STEP_RESIZE_WAKE) != 0;
    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = wake ? wake_read : -1, .events = POLLIN },
    };
    /* poll: "If the timeout argument is -1 ... the call shall block
     * indefinitely until at least one descriptor meets the specified
     * criteria or until the call is interrupted." An interrupted call
     * fails with EINTR; it is reported as nothing ready, and the caller
     * recomputes the time left. With SA_RESTART poll may instead be
     * restarted; the self-pipe then ends it (R111). */
    const int polled = poll(fds, wake ? 2 : 1, timeout_ms < 0 ? -1 : timeout_ms);
    if (polled < 0) {
        return errno == EINTR ? OVD_OK : OVD_ERROR_PLATFORM;
    }
    if (wake && (fds[1].revents & POLLIN) != 0) {
        wake_drain();
        *ready |= OVD_PLATFORM_READY_RESIZE;
    }
    /* poll sets POLLHUP, POLLERR, and POLLNVAL "even if the application
     * did not set the corresponding bit in events": stdin hung up or is
     * invalid, and would report ready forever. */
    const bool hung_up = (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
    if ((fds[0].revents & POLLIN) != 0) {
        /* A hang-up with input reported (macOS ptys report both, and read
         * then returns 0): the bytes still queued are read first, and the
         * read that finds none reports the hang-up. */
        if (hung_up) {
            const bool set = true;
            memcpy(modes->saved + offsetof(Saved, hung_up), &set, sizeof set);
        }
        *ready |= OVD_PLATFORM_READY_INPUT;
    } else if (hung_up) {
        return OVD_ERROR_PLATFORM;
    }
    return OVD_OK;
}

OvdResult ovd_platform_input_read(OvdPlatformModes *modes, char *bytes, size_t capacity,
                                  size_t *length, bool *resized)
{
    if (modes == nullptr || bytes == nullptr || length == nullptr || resized == nullptr
        || capacity < OVD_PLATFORM_READ_MIN || (modes->steps & STEP_RAW_INPUT) == 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    *length = 0;
    *resized = false;
    /* Above SSIZE_MAX "the result is implementation-defined". */
    const size_t chunk = capacity > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : capacity;
    for (;;) {
        /* VMIN and VTIME are 0 (Case D): read returns what is available,
         * or 0 without waiting. */
        const ssize_t got = read(STDIN_FILENO, bytes, chunk);
        bool hung_up = false;
        memcpy(&hung_up, modes->saved + offsetof(Saved, hung_up), sizeof hung_up);
        if (got == 0 && hung_up) {
            return OVD_ERROR_PLATFORM;
        }
        if (got >= 0) {
            *length = (size_t)got;
            return OVD_OK;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return OVD_OK;
        }
        return OVD_ERROR_PLATFORM;
    }
}

uint64_t ovd_platform_clock_ms(void)
{
    /* clock_gettime: "All implementations shall support a clock_id of
     * CLOCK_MONOTONIC", which "cannot be set via clock_settime()". */
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

/* ---- Size, output, environment --------------------------------------- */

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
