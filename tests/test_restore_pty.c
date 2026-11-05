/* Abnormal-exit restoration on a pseudo-terminal (OVD-19, T117): spec
 * FR-030 to FR-036, SC-003; research.md R112. POSIX only; Windows is
 * cross-built and stays manual (OVD-20).
 *
 * Each case runs two processes on a fresh pty:
 * - a leader, which makes the pty its controlling terminal, waits for the
 *   subject, then reports the subject's waitpid status and the pty's modes;
 * - the subject, which runs the case.
 * The leader stays alive past the subject because a session leader's exit
 * revokes the terminal on the BSDs, and the modes must be read after the
 * subject is gone. The leader ignores the keyboard signals; the subject
 * resets them. The subject reports what it saw over a pipe, one letter at
 * a time (write is async-signal-safe, POSIX.1-2024 XSH 2.4.3), so its
 * handlers can report too.
 *
 * The parent:
 * - drains the master;
 * - compares the modes after the subject ended with those before;
 * - checks the exit bytes;
 * - compares the subject's status with a control subject that does the
 *   same without Overdrive (SC-003).
 *
 * Both subjects run with RLIMIT_CORE 0, so the core flag compares equal
 * but is not exercised.
 *
 * The controlling terminal comes from setsid followed by ioctl
 * TIOCSCTTY. Linux's ioctl_tty(2): "Make the given terminal the
 * controlling terminal of the calling process. The calling process must
 * be a session leader and not have a controlling terminal already."
 * macOS documents the same request in termios(4). This is INFERRED for
 * both, and checked here: a control subject in canonical mode dies of
 * SIGINT when 0x03 is written to the master.
 *
 * SIGWINCH and TIOCSCTTY need the platform macros, as in
 * test_system_pty.c. */
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE

#include <overdrive/overdrive.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

/* The exit sequences, spelled from xterm's control-sequence reference as
 * in test_system_pty.c: CAN, SGR 0, the reports off in reverse, DECSET 7,
 * DECSET 25, DECRST 1049. */
#define CAN "\x18"
#define ENTER_MARK "\x1b[?1049h"
#define ENTER_ALTERNATE ENTER_MARK "\x1b[?25l\x1b[?7l"
#define EXIT_TAIL "\x1b[?7h\x1b[?25h\x1b[?1049l"
#define EXIT_ALTERNATE CAN "\x1b[0m" EXIT_TAIL
#define EXIT_REPORTS_ALTERNATE \
    CAN "\x1b[0m\x1b[?1004l\x1b[?2004l\x1b[?1006l\x1b[?1002l" EXIT_TAIL
#define LITERAL(text) (text), (sizeof(text) - 1)

/* Spec FR-030's POSIX signals. */
static const int FATAL[] = { SIGHUP, SIGINT,  SIGQUIT, SIGTERM, SIGABRT,
                             SIGSEGV, SIGBUS, SIGFPE,  SIGILL };
static constexpr size_t FATAL_COUNT = sizeof FATAL / sizeof FATAL[0];

/* ---- The subject's side ---------------------------------------------- */

static int report_fd = -1;

/* One letter to the parent; async-signal-safe. */
static void say(char letter)
{
    ssize_t written = 0;
    do {
        written = write(report_fd, &letter, 1);
    } while (written < 0 && errno == EINTR);
}

static OvdTerminalConfig config(bool input, bool restore)
{
    OvdTerminalConfig c = ovd_terminal_config_default();
    c.backend = OVD_BACKEND_SYSTEM;
    c.detect_color_depth = false;
    c.color_depth = OVD_COLOR_DEPTH_TRUECOLOR;
    c.alternate_screen = true;
    c.input = input;
    c.mouse = input;
    c.bracketed_paste = input;
    c.focus_events = input;
    c.restore_on_exit = restore;
    return c;
}

static bool create(OvdTerminalConfig c, OvdTerminal **terminal)
{
    const OvdResult result = ovd_terminal_create(&c, terminal);
    if (result != OVD_OK) {
        fprintf(stderr, "subject: create returned %s\n", ovd_result_name(result));
        return false;
    }
    return true;
}

/* The leader ignores these; the subject starts with every one at its
 * default action and none blocked. */
static const int RESET[] = { SIGHUP,  SIGINT,  SIGQUIT, SIGTERM, SIGABRT, SIGSEGV,  SIGBUS,
                             SIGFPE,  SIGILL,  SIGTSTP, SIGTTIN, SIGTTOU, SIGWINCH };

static void set_action(int signal, void (*handler)(int))
{
    struct sigaction action = { .sa_handler = handler };
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
}

static void reset_signals(void)
{
    for (size_t i = 0; i < sizeof RESET / sizeof RESET[0]; ++i) {
        set_action(RESET[i], SIG_DFL);
    }
    sigset_t none;
    sigemptyset(&none);
    /* The subject is single-threaded, where sigprocmask is specified. */
    sigprocmask(SIG_SETMASK, &none, nullptr);
}

/* A real fault: a write to a page with no access. Which signal that is
 * (SIGSEGV on Linux, SIGBUS on macOS) does not matter: the control does
 * the same. A pointer check sanitizer sees a valid pointer. */
static void fault(void)
{
    void *mapped = mmap(nullptr, (size_t)sysconf(_SC_PAGESIZE), PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        return;
    }
    volatile char *page = mapped;
    page[0] = 1;
}

/* parameter 0 is a real fault; otherwise the signal, sent with kill. */
static void end_with(int parameter)
{
    if (parameter == 0) {
        fault();
    } else {
        kill(getpid(), parameter);
    }
}

static bool subject_signal(int parameter)
{
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    end_with(parameter);
    say('x'); /* not reached */
    return false;
}

static bool control_signal(int parameter)
{
    end_with(parameter);
    say('x');
    return false;
}

/* US3 scenario 2: with input on, the keys arrive as Ctrl+C, Ctrl+\, and
 * Ctrl+Z (R105), and no signal is raised. */
static bool subject_keys(int)
{
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    say('R');
    const uint32_t expected[] = { 'c', '\\', 'z' };
    bool all = true;
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
        OvdEvent event = {};
        do {
            if (ovd_input_wait(terminal, 5000, &event) != OVD_OK
                || event.kind == OVD_EVENT_KIND_NONE) {
                all = false;
                break;
            }
        } while (event.kind != OVD_EVENT_KIND_KEY);
        all = all && event.key.key == OVD_KEY_CHAR && event.key.codepoint == expected[i]
              && (event.key.modifiers & OVD_MODIFIER_CTRL) != 0;
    }
    if (all) {
        say('k');
    }
    ovd_terminal_destroy(terminal);
    return true;
}

/* Canonical mode, no Overdrive: 0x03 must end it with SIGINT, which shows
 * the pty is its controlling terminal. */
static bool control_keys(int)
{
    say('R');
    sleep(10);
    return false;
}

/* A frame much larger than the pty buffer, so its write blocks. */
static void draw(OvdCanvas *canvas)
{
    const OvdSize size = ovd_canvas_size(canvas);
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            const OvdStyle style = {
                .fg = ovd_color_rgb((uint8_t)(x * 7), (uint8_t)(y * 13), (uint8_t)(x + y)),
                .bg = ovd_color_rgb((uint8_t)(255 - x), (uint8_t)(y * 3), (uint8_t)(x * y)),
                .attrs = OVD_ATTR_NONE,
            };
            ovd_canvas_text(canvas, x, y, "#", 1, style);
        }
    }
}

/* R112's partial frame: the parent stops reading, so the frame write
 * blocks, then sends SIGTERM. */
static bool subject_frame(int)
{
    OvdTerminal *terminal = nullptr;
    if (!create(config(false, true), &terminal)) {
        return false;
    }
    OvdCanvas *canvas = nullptr;
    if (ovd_frame_begin(terminal, &canvas) != OVD_OK) {
        return false;
    }
    draw(canvas);
    say('R');
    const OvdResult ended = ovd_frame_end(terminal);
    fprintf(stderr, "subject: frame_end returned %s\n", ovd_result_name(ended));
    say('x');
    return false;
}

static volatile sig_atomic_t application_terms;

static void application_term(int)
{
    application_terms = application_terms + 1;
    say('h');
}

/* True if fd 0 is in canonical mode with echo, as before create. */
static bool canonical(void)
{
    struct termios now;
    return tcgetattr(STDIN_FILENO, &now) == 0 && (now.c_lflag & ICANON) != 0
           && (now.c_lflag & ECHO) != 0;
}

/* FR-032: a termination signal the application handles is its own:
 * Overdrive calls the handler and restores nothing. */
static bool subject_term_before(int)
{
    set_action(SIGTERM, application_term);
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    kill(getpid(), SIGTERM);
    if (application_terms == 1 && !canonical()) {
        say('r');
    }
    ovd_terminal_destroy(terminal);
    if (canonical()) {
        say('d');
    }
    return true;
}

static bool is_application_term(const struct sigaction *action)
{
    return (action->sa_flags & SA_SIGINFO) == 0 && action->sa_handler == application_term;
}

/* FR-032: a handler installed after create stays after destroy. */
static bool subject_term_after(int)
{
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    set_action(SIGTERM, application_term);
    ovd_terminal_destroy(terminal);
    struct sigaction now;
    if (sigaction(SIGTERM, nullptr, &now) == 0 && is_application_term(&now)) {
        say('s');
    }
    return true;
}

static bool ignored(int signal)
{
    struct sigaction now;
    return sigaction(signal, nullptr, &now) == 0 && (now.sa_flags & SA_SIGINFO) == 0
           && now.sa_handler == SIG_IGN;
}

/* FR-032: a signal ignored before create stays ignored. */
static bool subject_hup_ignored(int)
{
    set_action(SIGHUP, SIG_IGN);
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    const bool during = ignored(SIGHUP);
    kill(getpid(), SIGHUP);
    ovd_terminal_destroy(terminal);
    if (during && ignored(SIGHUP)) {
        say('i');
    }
    return true;
}

static bool same_action(const struct sigaction *a, const struct sigaction *b)
{
    if (a->sa_flags != b->sa_flags) {
        return false;
    }
    return (a->sa_flags & SA_SIGINFO) != 0 ? a->sa_sigaction == b->sa_sigaction
                                           : a->sa_handler == b->sa_handler;
}

/* FR-035: restore_on_exit false installs no abnormal-exit handler; with
 * input on, only SIGWINCH is Overdrive's. Each action is snapshotted
 * after a round trip through sigaction, as test_system_pty.c does, since
 * a libc may add its own flags when it installs one. */
static bool subject_no_restore(int)
{
    struct sigaction before[FATAL_COUNT];
    for (size_t i = 0; i < FATAL_COUNT; ++i) {
        sigaction(FATAL[i], nullptr, &before[i]);
        sigaction(FATAL[i], &before[i], nullptr);
        sigaction(FATAL[i], nullptr, &before[i]);
    }
    struct sigaction winch_before;
    sigaction(SIGWINCH, nullptr, &winch_before);
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, false), &terminal)) {
        return false;
    }
    bool same = true;
    for (size_t i = 0; i < FATAL_COUNT; ++i) {
        struct sigaction now;
        same = same && sigaction(FATAL[i], nullptr, &now) == 0 && same_action(&now, &before[i]);
    }
    struct sigaction winch;
    if (same && sigaction(SIGWINCH, nullptr, &winch) == 0 && !same_action(&winch, &winch_before)) {
        say('n');
    }
    ovd_terminal_destroy(terminal);
    return true;
}

/* 001 R29, data-model: a second system terminal registers nothing, and
 * the state before the first create is restored. */
static bool subject_second(int)
{
    OvdTerminal *first = nullptr;
    OvdTerminal *second = nullptr;
    if (!create(config(true, true), &first)) {
        return false;
    }
    OvdTerminalConfig c = config(true, true);
    c.alternate_screen = false;
    if (!create(c, &second)) {
        return false;
    }
    kill(getpid(), SIGTERM);
    say('x');
    return false;
}

/* A crash after destroy writes nothing. */
static bool subject_crash_after(int)
{
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    ovd_terminal_destroy(terminal);
    kill(getpid(), SIGSEGV);
    say('x');
    return false;
}

/* The application's crash handler: reports whether the terminal was
 * already restored, then lets the fault end the process by default. */
static void application_crash(int signal)
{
    say(canonical() ? 'S' : 'U');
    set_action(signal, SIG_DFL);
}

static void install_application_crash(void)
{
    set_action(SIGSEGV, application_crash);
    set_action(SIGBUS, application_crash);
}

/* FR-032: for a crash signal the application handles, restoration runs
 * first, then the application's handler. */
static bool subject_app_crash(int)
{
    install_application_crash();
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    fault();
    say('x');
    return false;
}

static bool control_app_crash(int)
{
    install_application_crash();
    fault();
    say('x');
    return false;
}

static volatile sig_atomic_t recovered;

/* An application crash handler that recovers by returning: reports
 * whether the terminal was already restored. */
static void application_recover(int)
{
    recovered = recovered + 1;
    say(canonical() ? 'S' : 'U');
}

/* FR-031, FR-032 (R121): a crash signal sent with kill, whose
 * application handler returns, does not end the process, as without
 * Overdrive. The terminal is restored before the handler runs, and
 * destroy then writes nothing more (FR-033). */
static bool subject_recover(int signal)
{
    set_action(signal, application_recover);
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    kill(getpid(), signal);
    ovd_terminal_destroy(terminal);
    if (recovered == 1 && canonical()) {
        say('d');
    }
    return true;
}

static bool control_recover(int signal)
{
    set_action(signal, application_recover);
    kill(getpid(), signal);
    return true;
}

/* An SA_RESETHAND crash handler: reports whether the terminal was
 * restored, then returns (parameter 0, the fault repeats under SIG_DFL)
 * or re-raises (parameter 1, the crash-reporter pattern). */
static volatile sig_atomic_t reraise;

static void application_oneshot(int signal)
{
    say(canonical() ? 'S' : 'U');
    if (reraise) {
        raise(signal);
    }
}

static void install_oneshot(int parameter)
{
    reraise = parameter;
    const int signals[] = { SIGSEGV, SIGBUS };
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) {
        struct sigaction action = { .sa_handler = application_oneshot,
                                    .sa_flags = SA_RESETHAND };
        sigemptyset(&action.sa_mask);
        sigaction(signals[i], &action, nullptr);
    }
    /* A handler that loops ends the case by SIGALRM, not by hanging it. */
    alarm(5);
}

/* FR-031, FR-032 (R121): the application's SA_RESETHAND still resets,
 * so the fault ends the process as without Overdrive. */
static bool subject_oneshot(int parameter)
{
    install_oneshot(parameter);
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    fault();
    say('x');
    return false;
}

static bool control_oneshot(int parameter)
{
    install_oneshot(parameter);
    fault();
    say('x');
    return false;
}

/* FR-032: destroy puts every action back as it was before create. */
static bool subject_put_back(int)
{
    set_action(SIGTERM, application_term);
    set_action(SIGHUP, SIG_IGN);
    struct sigaction before[FATAL_COUNT];
    for (size_t i = 0; i < FATAL_COUNT; ++i) {
        sigaction(FATAL[i], nullptr, &before[i]);
        sigaction(FATAL[i], &before[i], nullptr);
        sigaction(FATAL[i], nullptr, &before[i]);
    }
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    ovd_terminal_destroy(terminal);
    bool same = true;
    for (size_t i = 0; i < FATAL_COUNT; ++i) {
        struct sigaction now;
        if (sigaction(FATAL[i], nullptr, &now) != 0 || !same_action(&now, &before[i])) {
            fprintf(stderr, "subject: signal %d not put back\n", FATAL[i]);
            same = false;
        }
    }
    if (same) {
        say('p');
    }
    return true;
}

/* R121: a create after a destroy owns the record again. */
static bool subject_recreate(int)
{
    OvdTerminal *terminal = nullptr;
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    ovd_terminal_destroy(terminal);
    if (!create(config(true, true), &terminal)) {
        return false;
    }
    kill(getpid(), SIGTERM);
    say('x');
    return false;
}

/* FR-033, R112: create and destroy forever; the parent sends SIGTERM at
 * a seeded delay. Every enter sequence is matched by exactly one exit. */
static bool subject_loop(int)
{
    say('R');
    for (;;) {
        OvdTerminal *terminal = nullptr;
        if (!create(config(true, true), &terminal)) {
            return false;
        }
        ovd_terminal_destroy(terminal);
    }
}

/* ---- The leader ------------------------------------------------------ */

typedef bool (*Body)(int parameter);

typedef struct Run {
    const char *name;
    Body body;
    int parameter;
    OvdSize size;
    /* Written to the master when the subject says 'R'. */
    const char *keys;
    size_t keys_length;
    /* Sent to the subject this many milliseconds after 'R', if not 0. */
    int kill_signal;
    int kill_delay_ms;
    /* Stop reading the master from 'R' until the signal is sent. */
    bool stall;
} Run;

typedef struct Ending {
    int status;
    struct termios modes;
} Ending;

static bool read_all(int fd, void *bytes, size_t length)
{
    char *at = bytes;
    while (length > 0) {
        const ssize_t got = read(fd, at, length);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return false;
        }
        at += got;
        length -= (size_t)got;
    }
    return true;
}

static bool write_all(int fd, const void *bytes, size_t length)
{
    const char *at = bytes;
    while (length > 0) {
        const ssize_t written = write(fd, at, length);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        at += written;
        length -= (size_t)written;
    }
    return true;
}

/* Runs in the leader: never returns. */
[[noreturn]] static void leader(const char *slave_name, const Run *run, int report, int control,
                                int go)
{
    if (setsid() < 0) {
        _exit(10);
    }
    const int fd = open(slave_name, O_RDWR);
    if (fd < 0 || ioctl(fd, TIOCSCTTY, 0) != 0 || dup2(fd, STDIN_FILENO) < 0
        || dup2(fd, STDOUT_FILENO) < 0) {
        _exit(11);
    }
    if (fd > STDOUT_FILENO) {
        close(fd);
    }
    const int keyboard[] = { SIGHUP, SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU };
    for (size_t i = 0; i < sizeof keyboard / sizeof keyboard[0]; ++i) {
        set_action(keyboard[i], SIG_IGN);
    }
    const struct rlimit no_core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &no_core);

    const pid_t subject = fork();
    if (subject < 0) {
        _exit(12);
    }
    if (subject == 0) {
        close(control);
        close(go);
        report_fd = report;
        reset_signals();
        _exit(run->body(run->parameter) ? 0 : 1);
    }
    close(report);
    if (!write_all(control, &subject, sizeof subject)) {
        _exit(13);
    }
    Ending ending = {};
    while (waitpid(subject, &ending.status, 0) < 0) {
        if (errno != EINTR) {
            _exit(14);
        }
    }
    if (tcgetattr(STDIN_FILENO, &ending.modes) != 0 || !write_all(control, &ending, sizeof ending)) {
        _exit(15);
    }
    char byte = 0;
    if (!read_all(go, &byte, 1)) {
        _exit(16);
    }
    _exit(0);
}

/* ---- The parent ------------------------------------------------------ */

typedef struct Outcome {
    struct termios initial;
    Ending ending;
    char report[64];
    size_t report_length;
} Outcome;

/* What the subject wrote to the pty in the last run. */
static char output[8u << 20];
static size_t output_length;

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Reads what the master holds; false only if the output buffer is full. */
static bool drain(int master)
{
    for (;;) {
        if (output_length == sizeof output) {
            fprintf(stderr, "output buffer full\n");
            return false;
        }
        const ssize_t got = read(master, output + output_length, sizeof output - output_length);
        if (got > 0) {
            output_length += (size_t)got;
            continue;
        }
        if (got < 0 && errno == EINTR) {
            continue;
        }
        return true;
    }
}

static bool open_pty(int *master, int *slave, char *name, size_t name_size, OvdSize size)
{
    *master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(*master >= 0 && grantpt(*master) == 0 && unlockpt(*master) == 0);
    const char *slave_name = ptsname(*master);
    CHECK(slave_name != nullptr && strlen(slave_name) < name_size);
    strcpy(name, slave_name);
    *slave = open(name, O_RDWR | O_NOCTTY);
    CHECK(*slave >= 0);
    struct termios modes;
    CHECK(tcgetattr(*slave, &modes) == 0);
    /* Output processing off, so the master reads the library's bytes;
     * canonical mode, echo, and ISIG as a shell leaves them. */
    modes.c_oflag &= ~(tcflag_t)OPOST;
    modes.c_lflag |= (tcflag_t)(ECHO | ICANON | ISIG);
    CHECK(tcsetattr(*slave, TCSANOW, &modes) == 0);
    const struct winsize window = { .ws_row = (unsigned short)size.rows,
                                    .ws_col = (unsigned short)size.cols };
    CHECK(ioctl(*slave, TIOCSWINSZ, &window) == 0);
    CHECK(fcntl(*master, F_SETFL, fcntl(*master, F_GETFL) | O_NONBLOCK) == 0);
    return true;
}

/* Runs one case to the subject's end; false on a harness failure. */
static bool run(const Run *r, Outcome *outcome)
{
    *outcome = (Outcome){};
    output_length = 0;
    int master = -1;
    int slave = -1;
    char name[256];
    if (!open_pty(&master, &slave, name, sizeof name, r->size)) {
        return false;
    }
    CHECK(tcgetattr(slave, &outcome->initial) == 0);
    int report[2];
    int control[2];
    int go[2];
    CHECK(pipe(report) == 0 && pipe(control) == 0 && pipe(go) == 0);
    fflush(stderr);
    const pid_t leader_pid = fork();
    CHECK(leader_pid >= 0);
    if (leader_pid == 0) {
        close(master);
        close(slave);
        close(report[0]);
        close(control[0]);
        close(go[1]);
        leader(name, r, report[1], control[1], go[0]);
    }
    close(report[1]);
    close(control[1]);
    close(go[0]);
    CHECK(fcntl(report[0], F_SETFL, fcntl(report[0], F_GETFL) | O_NONBLOCK) == 0);

    bool ok = true;
    pid_t subject = -1;
    if (!read_all(control[0], &subject, sizeof subject)) {
        fprintf(stderr, "%s: the leader did not start\n", r->name);
        ok = false;
    }
    const int64_t deadline = now_ms() + 30000;
    int64_t kill_at = -1;
    bool stalled = false;
    bool ended = !ok;
    while (!ended) {
        if (now_ms() > deadline) {
            fprintf(stderr, "%s: timed out\n", r->name);
            kill(subject, SIGKILL);
            ok = false;
            break;
        }
        if (kill_at >= 0 && now_ms() >= kill_at) {
            kill(subject, r->kill_signal);
            kill_at = -1;
            stalled = false;
        }
        struct pollfd fds[3] = {
            { .fd = report[0], .events = POLLIN },
            { .fd = control[0], .events = POLLIN },
            { .fd = stalled ? -1 : master, .events = POLLIN },
        };
        poll(fds, 3, 10);
        if (!stalled && !drain(master)) {
            ok = false;
        }
        char letters[32];
        const ssize_t got = read(report[0], letters, sizeof letters);
        for (ssize_t i = 0; i < got; ++i) {
            if (outcome->report_length < sizeof outcome->report) {
                outcome->report[outcome->report_length++] = letters[i];
            }
            if (letters[i] != 'R') {
                continue;
            }
            if (r->keys_length > 0 && !write_all(master, r->keys, r->keys_length)) {
                ok = false;
            }
            if (r->kill_signal != 0) {
                kill_at = now_ms() + r->kill_delay_ms;
                stalled = r->stall;
            }
        }
        if ((fds[1].revents & (POLLIN | POLLHUP)) != 0) {
            if (!read_all(control[0], &outcome->ending, sizeof outcome->ending)) {
                fprintf(stderr, "%s: the leader failed\n", r->name);
                ok = false;
            }
            ended = true;
        }
    }
    /* The subject is gone: read what is left until the master is quiet. */
    for (int quiet = 0; quiet < 10;) {
        const size_t before = output_length;
        struct pollfd fd = { .fd = master, .events = POLLIN };
        poll(&fd, 1, 10);
        if (!drain(master)) {
            ok = false;
            break;
        }
        quiet = output_length == before ? quiet + 1 : 0;
    }
    char byte = 0;
    write_all(go[1], &byte, 1);
    int leader_status = 0;
    waitpid(leader_pid, &leader_status, 0);
    if (!WIFEXITED(leader_status) || WEXITSTATUS(leader_status) != 0) {
        fprintf(stderr, "%s: leader status %#x\n", r->name, (unsigned)leader_status);
        ok = false;
    }
    close(report[0]);
    close(control[0]);
    close(go[1]);
    close(slave);
    close(master);
    return ok;
}

/* ---- Checks ---------------------------------------------------------- */

/* PENDIN, where defined, is kernel state the BSDs set when ICANON comes
 * back on; test_system_pty.c ignores it for the same reason. */
#if defined(PENDIN)
static constexpr tcflag_t LFLAG_STATE = PENDIN;
#else
static constexpr tcflag_t LFLAG_STATE = 0;
#endif

static bool modes_restored(const Outcome *o)
{
    const struct termios *a = &o->initial;
    const struct termios *b = &o->ending.modes;
    const bool same = a->c_iflag == b->c_iflag && a->c_oflag == b->c_oflag
                      && a->c_cflag == b->c_cflag
                      && (a->c_lflag & ~LFLAG_STATE) == (b->c_lflag & ~LFLAG_STATE)
                      && memcmp(a->c_cc, b->c_cc, sizeof a->c_cc) == 0;
    if (!same) {
        fprintf(stderr, "modes: c_lflag %#lx c_iflag %#lx, before %#lx %#lx\n",
                (unsigned long)b->c_lflag, (unsigned long)b->c_iflag, (unsigned long)a->c_lflag,
                (unsigned long)a->c_iflag);
    }
    return same;
}

static size_t count(const char *text, size_t length)
{
    size_t found = 0;
    for (size_t i = 0; i + length <= output_length; ++i) {
        if (memcmp(output + i, text, length) == 0) {
            ++found;
        }
    }
    return found;
}

static bool ends_with(const char *text, size_t length)
{
    return output_length >= length && memcmp(output + output_length - length, text, length) == 0;
}

static bool reported(const Outcome *o, char letter)
{
    return memchr(o->report, letter, o->report_length) != nullptr;
}

static bool status_is_signal(int status, int signal)
{
    if (!WIFSIGNALED(status) || WTERMSIG(status) != signal) {
        fprintf(stderr, "status %#x, expected signal %d\n", (unsigned)status, signal);
        return false;
    }
    return true;
}

static bool status_is_exit_0(int status)
{
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "status %#x, expected exit 0\n", (unsigned)status);
        return false;
    }
    return true;
}

static bool same_status(const Outcome *subject, const Outcome *control)
{
    if (subject->ending.status != control->ending.status) {
        fprintf(stderr, "status %#x, control %#x\n", (unsigned)subject->ending.status,
                (unsigned)control->ending.status);
        return false;
    }
    return true;
}

static constexpr OvdSize SMALL = { 20, 6 };

/* SC-003: each FR-030 signal, and a real fault (parameter 0). */
static bool case_signal(int parameter)
{
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_signal, .parameter = parameter,
                      .size = SMALL },
              &control));
    CHECK(WIFSIGNALED(control.ending.status));
    CHECK(run(&(Run){ .name = "subject", .body = subject_signal, .parameter = parameter,
                      .size = SMALL },
              &subject));
    CHECK(same_status(&subject, &control));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(ENTER_MARK)) == 1);
    CHECK(count(LITERAL(CAN)) == 1);
    CHECK(ends_with(LITERAL(EXIT_REPORTS_ALTERNATE)));
    return true;
}

static bool case_keys(int)
{
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_keys, .size = SMALL,
                      .keys = "\x03", .keys_length = 1 },
              &control));
    CHECK(status_is_signal(control.ending.status, SIGINT));
    CHECK(run(&(Run){ .name = "subject", .body = subject_keys, .size = SMALL,
                      .keys = "\x03\x1c\x1a", .keys_length = 3 },
              &subject));
    CHECK(status_is_exit_0(subject.ending.status));
    CHECK(reported(&subject, 'k'));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(CAN)) == 1);
    return true;
}

static bool count_sink(void *context, const char *, size_t length)
{
    *(size_t *)context += length;
    return true;
}

/* The bytes of the frame subject_frame draws, from a headless terminal. */
static bool full_frame_length(OvdSize size, size_t *length)
{
    *length = 0;
    OvdTerminalConfig c = ovd_terminal_config_default();
    c.backend = OVD_BACKEND_HEADLESS;
    c.headless_size = size;
    c.color_depth = OVD_COLOR_DEPTH_TRUECOLOR;
    c.sink = count_sink;
    c.sink_context = length;
    OvdTerminal *headless = nullptr;
    CHECK(ovd_terminal_create(&c, &headless) == OVD_OK);
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(headless, &canvas) == OVD_OK);
    draw(canvas);
    CHECK(ovd_frame_end(headless) == OVD_OK);
    ovd_terminal_destroy(headless);
    return true;
}

/* R112's partial frame: the write is cut in the middle, and the exit
 * sequence after it is whole, starting with CAN. */
static bool case_frame(int)
{
    const OvdSize size = { 200, 60 };
    size_t full = 0;
    CHECK(full_frame_length(size, &full));
    Outcome subject;
    CHECK(run(&(Run){ .name = "subject", .body = subject_frame, .size = size,
                      .kill_signal = SIGTERM, .kill_delay_ms = 300, .stall = true },
              &subject));
    CHECK(status_is_signal(subject.ending.status, SIGTERM));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(CAN)) == 1);
    CHECK(ends_with(LITERAL(EXIT_ALTERNATE)));
    const size_t frame_written
        = output_length - (sizeof ENTER_ALTERNATE - 1) - (sizeof EXIT_ALTERNATE - 1);
    if (frame_written >= full) {
        fprintf(stderr, "the frame was written whole (%zu of %zu bytes)\n", frame_written, full);
        return false;
    }
    return true;
}

static bool case_term_before(int)
{
    Outcome subject;
    CHECK(run(&(Run){ .name = "subject", .body = subject_term_before, .size = SMALL }, &subject));
    CHECK(status_is_exit_0(subject.ending.status));
    CHECK(reported(&subject, 'h'));
    CHECK(reported(&subject, 'r'));
    CHECK(reported(&subject, 'd'));
    CHECK(count(LITERAL(CAN)) == 1);
    CHECK(modes_restored(&subject));
    return true;
}

static bool case_exits_reporting(Body body, char letter)
{
    Outcome subject;
    CHECK(run(&(Run){ .name = "subject", .body = body, .size = SMALL }, &subject));
    CHECK(status_is_exit_0(subject.ending.status));
    CHECK(reported(&subject, letter));
    CHECK(modes_restored(&subject));
    return true;
}

static bool case_term_after(int)
{
    return case_exits_reporting(subject_term_after, 's');
}

static bool case_hup_ignored(int)
{
    return case_exits_reporting(subject_hup_ignored, 'i');
}

static bool case_no_restore(int)
{
    return case_exits_reporting(subject_no_restore, 'n');
}

static bool case_second(int)
{
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_signal, .parameter = SIGTERM,
                      .size = SMALL },
              &control));
    CHECK(run(&(Run){ .name = "subject", .body = subject_second, .size = SMALL }, &subject));
    CHECK(same_status(&subject, &control));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(CAN)) == 1);
    CHECK(ends_with(LITERAL(EXIT_REPORTS_ALTERNATE)));
    return true;
}

static bool case_crash_after(int)
{
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_signal, .parameter = SIGSEGV,
                      .size = SMALL },
              &control));
    CHECK(run(&(Run){ .name = "subject", .body = subject_crash_after, .size = SMALL }, &subject));
    CHECK(same_status(&subject, &control));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(CAN)) == 1);
    CHECK(ends_with(LITERAL(EXIT_REPORTS_ALTERNATE)));
    return true;
}

static bool case_app_crash(int)
{
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_app_crash, .size = SMALL }, &control));
    CHECK(WIFSIGNALED(control.ending.status));
    CHECK(run(&(Run){ .name = "subject", .body = subject_app_crash, .size = SMALL }, &subject));
    CHECK(same_status(&subject, &control));
    CHECK(reported(&subject, 'S'));
    CHECK(!reported(&subject, 'U'));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(CAN)) == 1);
    return true;
}

static bool case_recover(int signal)
{
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_recover, .parameter = signal,
                      .size = SMALL },
              &control));
    CHECK(status_is_exit_0(control.ending.status));
    CHECK(run(&(Run){ .name = "subject", .body = subject_recover, .parameter = signal,
                      .size = SMALL },
              &subject));
    CHECK(same_status(&subject, &control));
    CHECK(reported(&subject, 'S'));
    CHECK(!reported(&subject, 'U'));
    CHECK(reported(&subject, 'd'));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(CAN)) == 1);
    return true;
}

/* True if sigaction reports SA_RESETHAND when an action is read back.
 * macOS does not (R121), so Overdrive cannot apply it there, and an
 * SA_RESETHAND handler that returns from a real fault is not covered. */
static bool resethand_reported(void)
{
    struct sigaction saved;
    sigaction(SIGUSR1, nullptr, &saved);
    struct sigaction action = { .sa_handler = application_oneshot, .sa_flags = SA_RESETHAND };
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, nullptr);
    struct sigaction now;
    const bool reported = sigaction(SIGUSR1, nullptr, &now) == 0
                          && (now.sa_flags & SA_RESETHAND) != 0;
    sigaction(SIGUSR1, &saved, nullptr);
    return reported;
}

static bool case_oneshot(int parameter)
{
    if (parameter == 0 && !resethand_reported()) {
        fprintf(stderr, "skipped: this platform does not report SA_RESETHAND (R121)\n");
        return true;
    }
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_oneshot, .parameter = parameter,
                      .size = SMALL },
              &control));
    CHECK(WIFSIGNALED(control.ending.status) && WTERMSIG(control.ending.status) != SIGALRM);
    CHECK(run(&(Run){ .name = "subject", .body = subject_oneshot, .parameter = parameter,
                      .size = SMALL },
              &subject));
    CHECK(same_status(&subject, &control));
    CHECK(reported(&subject, 'S'));
    CHECK(!reported(&subject, 'U'));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(CAN)) == 1);
    return true;
}

static bool case_put_back(int)
{
    return case_exits_reporting(subject_put_back, 'p');
}

static bool case_recreate(int)
{
    Outcome control;
    Outcome subject;
    CHECK(run(&(Run){ .name = "control", .body = control_signal, .parameter = SIGTERM,
                      .size = SMALL },
              &control));
    CHECK(run(&(Run){ .name = "subject", .body = subject_recreate, .size = SMALL }, &subject));
    CHECK(same_status(&subject, &control));
    CHECK(modes_restored(&subject));
    CHECK(count(LITERAL(ENTER_MARK)) == 2);
    CHECK(count(LITERAL(CAN)) == 2);
    CHECK(ends_with(LITERAL(EXIT_REPORTS_ALTERNATE)));
    return true;
}

/* The seed picks the delay; the parameter is the seed. */
static bool case_loop(int seed)
{
    const int delay = 1 + (int)(((unsigned)seed * 2654435761u) % 150u);
    Outcome subject;
    CHECK(run(&(Run){ .name = "subject", .body = subject_loop, .size = SMALL,
                      .kill_signal = SIGTERM, .kill_delay_ms = delay },
              &subject));
    CHECK(status_is_signal(subject.ending.status, SIGTERM));
    CHECK(modes_restored(&subject));
    const size_t enters = count(LITERAL(ENTER_MARK));
    const size_t exits = count(LITERAL(CAN));
    fprintf(stderr, "seed %d, delay %d ms: %zu enters, %zu exits\n", seed, delay, enters, exits);
    return enters >= 1 && enters == exits;
}

static bool report_case(const char *name, int parameter, bool (*body)(int))
{
    const bool ok = body(parameter);
    fprintf(stderr, "%s (%d): %s\n", name, parameter, ok ? "ok" : "FAILED");
    return ok;
}

int main(void)
{
    /* A closed standard descriptor would let a pty land on it. */
    for (int fd = 0; fd <= 2; ++fd) {
        if (fcntl(fd, F_GETFD) == -1 && open("/dev/null", O_RDWR) != fd) {
            return 1;
        }
    }
    bool ok = true;
    for (size_t i = 0; i < FATAL_COUNT; ++i) {
        ok = report_case("signal", FATAL[i], case_signal) && ok;
    }
    ok = report_case("real fault", 0, case_signal) && ok;
    ok = report_case("keys", 0, case_keys) && ok;
    ok = report_case("partial frame", 0, case_frame) && ok;
    ok = report_case("SIGTERM handler before create", 0, case_term_before) && ok;
    ok = report_case("SIGTERM handler after create", 0, case_term_after) && ok;
    ok = report_case("SIGHUP ignored", 0, case_hup_ignored) && ok;
    ok = report_case("restore_on_exit false", 0, case_no_restore) && ok;
    ok = report_case("second system terminal", 0, case_second) && ok;
    ok = report_case("crash after destroy", 0, case_crash_after) && ok;
    ok = report_case("application crash handler", 0, case_app_crash) && ok;
    static const int RECOVERABLE[] = { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL };
    for (size_t i = 0; i < sizeof RECOVERABLE / sizeof RECOVERABLE[0]; ++i) {
        ok = report_case("application crash handler returns", RECOVERABLE[i], case_recover) && ok;
    }
    ok = report_case("SA_RESETHAND crash handler returns", 0, case_oneshot) && ok;
    ok = report_case("SA_RESETHAND crash handler re-raises", 1, case_oneshot) && ok;
    ok = report_case("actions put back", 0, case_put_back) && ok;
    ok = report_case("create after destroy", 0, case_recreate) && ok;
    for (int seed = 1; seed <= 8; ++seed) {
        ok = report_case("create and destroy under SIGTERM", seed, case_loop) && ok;
    }
    fprintf(stderr, "test_restore_pty: %s\n", ok ? "all checks passed" : "FAILED");
    return ok ? 0 : 1;
}
