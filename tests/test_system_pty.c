/* The system backend's lifecycle on a pseudo-terminal the test opens
 * itself (#43): FR-010, FR-011, FR-014; research.md R25 to R28, R46.
 * POSIX only; Windows stays manual (OVD-12).
 *
 * Each case opens a fresh pty, puts its slave on stdin and stdout, runs
 * the library through the public API, and reads what it wrote from the
 * master. The test checks the enter and exit bytes, the terminal modes
 * after create and after destroy, create's rollbacks, the per-frame size
 * read, and a 1x1 terminal. The library only ever sees the pty: the
 * test's own stdin and stdout are restored after every case, pass or
 * fail, and the developer's terminal is never changed.
 *
 * What was written is read up to a sentinel the test writes after the
 * library, so "wrote nothing" is an exact check with no timing involved.
 * The library cannot write the sentinel's C0 controls (R7). Frame bytes
 * are compared with a headless terminal (R35) that draws the same frames:
 * the renderer is test_diff's subject; this test checks that the system
 * backend writes each frame, whole, at the size it read.
 *
 * With input on (OVD-18, T114; R106, R111, R115, R116), the test also
 * checks raw mode and its restore, bytes written to the master arriving
 * as events, resize wakeups, timing, the report bytes, and an application
 * SIGWINCH handler. The pty is not the test's controlling terminal, so a
 * resize is delivered by kill(getpid(), SIGWINCH) after TIOCSWINSZ.
 *
 * The pty calls are POSIX.1-2024 (XSI): posix_openpt, grantpt, unlockpt,
 * ptsname, tcgetattr, tcsetattr. TIOCSWINSZ is not in POSIX: it is
 * documented beside TIOCGWINSZ in the sources src/platform/platform_posix.c
 * cites.
 *
 * SIGWINCH is POSIX.1-2024, newer than the 700 level requested here: the
 * macOS SDK's <sys/signal.h> defines it only without _POSIX_C_SOURCE or
 * with _DARWIN_C_SOURCE, and glibc exposes its non-2008 names with
 * _DEFAULT_SOURCE. Each macro does nothing on the other system. */
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE

#include "support/test_alloc.h"
#include "vt_replay.h"

#include <overdrive/overdrive.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
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

#define CHECK_RESULT(call, expected) \
    do { \
        const OvdResult check_result_ = (call); \
        if (check_result_ != (expected)) { \
            fprintf(stderr, "%s:%d: %s returned %s, expected %s\n", __FILE__, __LINE__, #call, \
                    ovd_result_name(check_result_), ovd_result_name(expected)); \
            return false; \
        } \
    } while (false)

/* R26's sequences, spelled here from xterm's control-sequence reference
 * (invisible-island.net/xterm/ctlseqs/ctlseqs.html), not taken from
 * terminal_system.c. Enter: DECSET 1049 (alternate screen, only when
 * requested), DECRST 25 (hide the cursor), DECRST 7 (autowrap off), then
 * the requested reports (R116): DECSET 1002 and 1006 (mouse), 2004
 * (bracketed paste), 1004 (focus). Exit, in terminal.h's destroy order:
 * CAN (0x18), SGR 0, the reports turned off in reverse, DECSET 7, DECSET
 * 25, then DECRST 1049, or CUP to column 1 of the last ended frame's last
 * row (R46). */
#define CAN "\x18"
#define ENTER_ALTERNATE "\x1b[?1049h\x1b[?25l\x1b[?7l"
#define ENTER_PRIMARY "\x1b[?25l\x1b[?7l"
#define REPORTS_ON "\x1b[?1002h\x1b[?1006h\x1b[?2004h\x1b[?1004h"
#define REPORTS_OFF "\x1b[?1004l\x1b[?2004l\x1b[?1006l\x1b[?1002l"
#define EXIT_TAIL "\x1b[?7h\x1b[?25h"
#define EXIT_COMMON CAN "\x1b[0m" EXIT_TAIL
#define EXIT_ALTERNATE EXIT_COMMON "\x1b[?1049l"
#define EXIT_AT_ROW(rows) EXIT_COMMON "\x1b[" #rows ";1H"
#define EXIT_REPORTS_ALTERNATE CAN "\x1b[0m" REPORTS_OFF EXIT_TAIL "\x1b[?1049l"
#define FULL_REDRAW "\x1b[2J"

#define LITERAL(text) (text), (sizeof(text) - 1)

/* Written by the test after the library; the library never writes C0
 * controls other than ESC and CAN, so the first match is this write. */
static const char SENTINEL[] = "\x01" "sentinel" "\x02";
static constexpr size_t SENTINEL_LENGTH = sizeof SENTINEL - 1;
/* How long one read of the master may wait: a hang guard only. */
static constexpr int READ_TIMEOUT_MS = 5000;

static bool size_equal(OvdSize a, OvdSize b)
{
    return a.cols == b.cols && a.rows == b.rows;
}

/* ---- The pty --------------------------------------------------------- */

typedef struct Pty {
    int master;
    int slave;      /* read-write: modes are read and the sentinel written here */
    int saved[2];   /* the test's own fds 0 and 1, restored by pty_close */
    struct termios initial; /* the slave's modes before any library call */
} Pty;

static bool write_all(int fd, const char *bytes, size_t length)
{
    while (length > 0) {
        const ssize_t written = write(fd, bytes, length);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            perror("write");
            return false;
        }
        bytes += written;
        length -= (size_t)written;
    }
    return true;
}

static bool pty_resize(const Pty *pty, int32_t cols, int32_t rows)
{
    const struct winsize size = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols };
    if (ioctl(pty->slave, TIOCSWINSZ, &size) != 0) {
        perror("TIOCSWINSZ");
        return false;
    }
    return true;
}

/* Whether fd is the character device described by device. */
static bool is_device(int fd, const struct stat *device)
{
    struct stat st;
    return fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) && st.st_rdev == device->st_rdev;
}

/* Restores the test's fds 0 and 1 and closes the pty. Safe on a
 * partly opened Pty. */
static void pty_close(Pty *pty)
{
    for (int fd = 0; fd < 2; ++fd) {
        if (pty->saved[fd] >= 0) {
            if (dup2(pty->saved[fd], fd) < 0) {
                perror("restoring a standard descriptor");
            }
            close(pty->saved[fd]);
            pty->saved[fd] = -1;
        }
    }
    if (pty->slave >= 0) {
        close(pty->slave);
        pty->slave = -1;
    }
    if (pty->master >= 0) {
        close(pty->master);
        pty->master = -1;
    }
}

/* Where pty_open puts the slave. */
typedef enum Placement : uint8_t {
    PLACE_BOTH,             /* fds 0 and 1 */
    PLACE_READ_ONLY_STDOUT, /* fd 1 opened read-only: every write fails */
    PLACE_NULL_STDIN,       /* fd 1 only; fd 0 is /dev/null */
} Placement;

/* Opens a pty of the given size with echo on and output processing off,
 * and puts its slave on fds 0 and 1 as placement says. O_NOCTTY keeps it
 * from becoming the test's controlling terminal, so no SIGHUP, SIGWINCH,
 * or SIGTTOU comes from it. Fails, with nothing left open or changed,
 * unless fd 1 is then the slave, and fd 0 the slave or /dev/null. */
static bool pty_open(Pty *pty, OvdSize size, Placement placement)
{
    const bool read_only_stdout = placement == PLACE_READ_ONLY_STDOUT;
    *pty = (Pty){ .master = -1, .slave = -1, .saved = { -1, -1 } };
    pty->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty->master < 0 || grantpt(pty->master) != 0 || unlockpt(pty->master) != 0) {
        perror("posix_openpt");
        pty_close(pty);
        return false;
    }
    const char *name = ptsname(pty->master);
    if (name == nullptr || (pty->slave = open(name, O_RDWR | O_NOCTTY)) < 0) {
        perror("opening the pty slave");
        pty_close(pty);
        return false;
    }
    struct termios modes;
    if (tcgetattr(pty->slave, &modes) != 0) {
        perror("tcgetattr");
        pty_close(pty);
        return false;
    }
    modes.c_oflag &= ~(tcflag_t)OPOST;
    modes.c_lflag |= (tcflag_t)ECHO;
    if (tcsetattr(pty->slave, TCSANOW, &modes) != 0 || tcgetattr(pty->slave, &pty->initial) != 0
        || (pty->initial.c_lflag & ECHO) == 0 || (pty->initial.c_oflag & OPOST) != 0) {
        fprintf(stderr, "could not set the pty's modes\n");
        pty_close(pty);
        return false;
    }
    if (!pty_resize(pty, size.cols, size.rows)) {
        pty_close(pty);
        return false;
    }

    const int out = read_only_stdout ? open(name, O_RDONLY | O_NOCTTY) : pty->slave;
    if (out < 0) {
        perror("opening the pty slave read-only");
        pty_close(pty);
        return false;
    }
    const int in = placement == PLACE_NULL_STDIN ? open("/dev/null", O_RDONLY) : pty->slave;
    if (in < 0) {
        perror("opening /dev/null");
        if (out != pty->slave) {
            close(out);
        }
        pty_close(pty);
        return false;
    }
    pty->saved[0] = dup(STDIN_FILENO);
    pty->saved[1] = dup(STDOUT_FILENO);
    const bool placed = pty->saved[0] >= 0 && pty->saved[1] >= 0
                        && dup2(in, STDIN_FILENO) == STDIN_FILENO
                        && dup2(out, STDOUT_FILENO) == STDOUT_FILENO;
    if (out != pty->slave) {
        close(out);
    }
    if (in != pty->slave) {
        close(in);
    }
    struct stat slave;
    if (!placed || fstat(pty->slave, &slave) != 0
        || (placement != PLACE_NULL_STDIN && !is_device(STDIN_FILENO, &slave))
        || (placement == PLACE_NULL_STDIN && isatty(STDIN_FILENO))
        || !is_device(STDOUT_FILENO, &slave)) {
        fprintf(stderr, "could not put the pty on stdin and stdout\n");
        pty_close(pty);
        return false;
    }
    return true;
}

/* ---- What the library wrote ------------------------------------------ */

typedef struct Output {
    char bytes[64 * 1024];
    size_t length;
} Output;

static void dump(const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        const unsigned char byte = (unsigned char)bytes[i];
        if (byte >= 0x20 && byte < 0x7F && byte != '\\') {
            fputc(byte, stderr);
        } else {
            fprintf(stderr, "\\x%02x", byte);
        }
    }
}

/* Everything written to the pty since the last call: writes the sentinel
 * after it, then reads the master up to the sentinel. */
static bool read_output(const Pty *pty, Output *out)
{
    out->length = 0;
    if (!write_all(pty->slave, SENTINEL, SENTINEL_LENGTH)) {
        return false;
    }
    while (out->length < SENTINEL_LENGTH
           || memcmp(out->bytes + out->length - SENTINEL_LENGTH, SENTINEL, SENTINEL_LENGTH) != 0) {
        if (out->length == sizeof out->bytes) {
            fprintf(stderr, "the output exceeds the capture buffer\n");
            return false;
        }
        struct pollfd ready = { .fd = pty->master, .events = POLLIN };
        const int polled = poll(&ready, 1, READ_TIMEOUT_MS);
        if (polled < 0 && errno == EINTR) {
            continue;
        }
        if (polled <= 0) {
            fprintf(stderr, "no sentinel from the pty within %d ms\n", READ_TIMEOUT_MS);
            return false;
        }
        const ssize_t got = read(pty->master, out->bytes + out->length,
                                 sizeof out->bytes - out->length);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            perror("reading the pty master");
            return false;
        }
        out->length += (size_t)got;
    }
    out->length -= SENTINEL_LENGTH;
    return true;
}

/* The library wrote exactly length bytes of expected since the last
 * check. */
static bool expect_output(const Pty *pty, const char *expected, size_t length, const char *what)
{
    static Output output;
    if (!read_output(pty, &output)) {
        fprintf(stderr, "%s: could not read the output\n", what);
        return false;
    }
    if (output.length != length || memcmp(output.bytes, expected, length) != 0) {
        fprintf(stderr, "%s: wrote \"", what);
        dump(output.bytes, output.length);
        fprintf(stderr, "\", expected \"");
        dump(expected, length);
        fprintf(stderr, "\"\n");
        return false;
    }
    return true;
}

static bool contains(const char *bytes, size_t length, const char *needle)
{
    const size_t needle_length = strlen(needle);
    for (size_t i = 0; i + needle_length <= length; ++i) {
        if (memcmp(bytes + i, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

/* ---- Terminal modes -------------------------------------------------- */

/* PENDIN, where defined, is kernel state, not a mode: the BSD kernels set
 * it themselves when ICANON is turned back on, so pending input is retyped
 * (the macOS SDK's sys/termios.h marks it "(state)"). Restoring raw input
 * sets it; it is the only bit ignored. */
#if defined(PENDIN)
static constexpr tcflag_t LFLAG_STATE = PENDIN;
#else
static constexpr tcflag_t LFLAG_STATE = 0;
#endif

static bool modes_equal(const struct termios *a, const struct termios *b)
{
    return a->c_iflag == b->c_iflag && a->c_oflag == b->c_oflag && a->c_cflag == b->c_cflag
           && (a->c_lflag & ~LFLAG_STATE) == (b->c_lflag & ~LFLAG_STATE)
           && memcmp(a->c_cc, b->c_cc, sizeof a->c_cc) == 0
           && cfgetispeed(a) == cfgetispeed(b) && cfgetospeed(a) == cfgetospeed(b);
}

/* The pty's modes are its initial ones, with ECHO cleared if echo_off
 * and nothing else changed. */
static bool modes_are(const Pty *pty, bool echo_off)
{
    struct termios expected = pty->initial;
    if (echo_off) {
        expected.c_lflag &= ~(tcflag_t)ECHO;
    }
    struct termios now;
    if (tcgetattr(pty->slave, &now) != 0) {
        perror("tcgetattr");
        return false;
    }
    if (!modes_equal(&expected, &now)) {
        fprintf(stderr, "pty modes: c_lflag %#lx, expected %#lx (ECHO is %#lx)\n",
                (unsigned long)now.c_lflag, (unsigned long)expected.c_lflag,
                (unsigned long)ECHO);
        return false;
    }
    return true;
}

/* ---- Sessions -------------------------------------------------------- */

typedef struct Capture {
    char bytes[16 * 1024];
    size_t length;
} Capture;

/* The headless twin's sink: keeps one frame's bytes. */
static bool capture_sink(void *context, const char *bytes, size_t length)
{
    Capture *capture = context;
    if (length > sizeof capture->bytes - capture->length) {
        return false;
    }
    memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return true;
}

typedef struct Session {
    Pty pty;
    int parameter;
    TestAlloc alloc;
    OvdAllocator allocator; /* the system terminal's, counted */
    OvdTerminal *system;
    OvdTerminal *headless;  /* the twin, on the default allocator */
    Capture capture;
} Session;

/* The system backend with a fixed depth, so frames match the twin's. */
static OvdTerminalConfig system_config(Session *s, bool alternate_screen)
{
    OvdTerminalConfig c = ovd_terminal_config_default();
    c.detect_color_depth = false;
    c.color_depth = OVD_COLOR_DEPTH_TRUECOLOR;
    c.alternate_screen = alternate_screen;
    c.allocator = &s->allocator;
    return c;
}

static bool create_twin(Session *s, OvdSize size)
{
    OvdTerminalConfig c = ovd_terminal_config_default();
    c.backend = OVD_BACKEND_HEADLESS;
    c.color_depth = OVD_COLOR_DEPTH_TRUECOLOR;
    c.headless_size = size;
    c.sink = capture_sink;
    c.sink_context = &s->capture;
    CHECK_RESULT(ovd_terminal_create(&c, &s->headless), OVD_OK);
    CHECK(ovd_terminal_color_depth(s->headless) == ovd_terminal_color_depth(s->system));
    return true;
}

/* Every frame's content: a styled label, a wide glyph at the last column
 * (its inside half is drawn as a space, render.h), and a marker in the
 * bottom-right cell that variant changes. */
static void draw(OvdCanvas *canvas, int variant)
{
    const OvdSize size = ovd_canvas_size(canvas);
    const OvdStyle label = {
        .fg = ovd_color_rgb(255, 128, 0), .bg = OVD_COLOR_DEFAULT, .attrs = OVD_ATTR_BOLD
    };
    ovd_canvas_text(canvas, 0, 0, LITERAL("pty"), label);
    ovd_canvas_text(canvas, size.cols - 1, 0, LITERAL("\xe7\x95\x8c"), OVD_STYLE_DEFAULT);
    ovd_canvas_text(canvas, size.cols - 1, size.rows - 1, variant == 0 ? "#" : "%", 1,
                    OVD_STYLE_DEFAULT);
}

/* Opens a frame on both terminals, at size, and draws variant on both. */
static bool begin_both(Session *s, OvdSize size, int variant)
{
    OvdCanvas *system = nullptr;
    OvdCanvas *headless = nullptr;
    CHECK_RESULT(ovd_frame_begin(s->system, &system), OVD_OK);
    CHECK_RESULT(ovd_frame_begin(s->headless, &headless), OVD_OK);
    CHECK(size_equal(ovd_canvas_size(system), size));
    CHECK(size_equal(ovd_canvas_size(headless), size));
    CHECK(size_equal(ovd_terminal_size(s->system), size));
    draw(system, variant);
    draw(headless, variant);
    return true;
}

/* Ends both frames: the pty received exactly the twin's bytes, which
 * stay in s->capture. */
static bool end_both(Session *s, const char *what)
{
    s->capture.length = 0;
    CHECK_RESULT(ovd_frame_end(s->system), OVD_OK);
    CHECK_RESULT(ovd_frame_end(s->headless), OVD_OK);
    return expect_output(&s->pty, s->capture.bytes, s->capture.length, what);
}

static bool frame(Session *s, OvdSize size, int variant, const char *what)
{
    return begin_both(s, size, variant) && end_both(s, what);
}

/* The replay oracle's classifier (R38): the test's text is ASCII except
 * U+754C, which is wide. */
static TestWidth classify(uint32_t code_point)
{
    return code_point == 0x754C ? TEST_WIDTH_WIDE : TEST_WIDTH_NARROW;
}

/* Replayed alone on a blank screen of size, the captured frame is a valid
 * full redraw whose bottom-right cell holds marker. */
static bool replay_shows(const Capture *capture, OvdSize size, const char *marker)
{
    VtReplay *vt = vt_replay_create(size.cols, size.rows, classify);
    CHECK(vt != nullptr);
    bool ok = vt_replay_feed(vt, capture->bytes, capture->length);
    if (!ok) {
        fprintf(stderr, "replay at %dx%d: %s\n", size.cols, size.rows, vt_replay_error(vt));
    }
    const TestCell *cell
        = ok ? test_screen_cell(vt_replay_screen(vt), size.cols - 1, size.rows - 1) : nullptr;
    if (ok && (cell == nullptr || strcmp(cell->text, marker) != 0)) {
        fprintf(stderr, "replay at %dx%d: bottom-right cell is \"%s\", expected \"%s\"\n",
                size.cols, size.rows, cell != nullptr ? cell->text : "(none)", marker);
        ok = false;
    }
    vt_replay_destroy(vt);
    return ok;
}

/* ---- Cases ----------------------------------------------------------- */

/* Create writes the enter sequence and turns echo off, changing no other
 * mode; destroy writes the exit sequence, restores every mode, and frees
 * every block. */
static bool lifecycle(Session *s, bool alternate_screen)
{
    const OvdTerminalConfig c = system_config(s, alternate_screen);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    /* Create's whole batch; the refusal cases cover each of these. */
    CHECK(s->alloc.allocate_calls == 4);
    CHECK(modes_are(&s->pty, true));
    if (alternate_screen) {
        CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE), "enter"));
    } else {
        CHECK(expect_output(&s->pty, LITERAL(ENTER_PRIMARY), "enter"));
    }

    ovd_terminal_destroy(s->system);
    s->system = nullptr;
    if (alternate_screen) {
        CHECK(expect_output(&s->pty, LITERAL(EXIT_ALTERNATE), "exit"));
    } else {
        /* No frame ended: the row is the size read at create's (R46). */
        CHECK(expect_output(&s->pty, LITERAL(EXIT_AT_ROW(4)), "exit"));
    }
    CHECK(modes_are(&s->pty, false));
    CHECK(s->alloc.release_calls == 4);
    return true;
}

static bool case_alternate(Session *s)
{
    return lifecycle(s, true);
}

static bool case_primary(Session *s)
{
    return lifecycle(s, false);
}

/* create fails with expected, leaves *out as it was, and changes no mode. */
static bool create_fails(Session *s, OvdResult expected)
{
    OvdTerminalConfig c = ovd_terminal_config_default();
    c.allocator = &s->allocator;
    OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
    OvdTerminal *out = sentinel;
    CHECK_RESULT(ovd_terminal_create(&c, &out), expected);
    CHECK(out == sentinel);
    CHECK(modes_are(&s->pty, false));
    CHECK(s->alloc.live_blocks == 0);
    return true;
}

/* Step 6 refused: at allocate call `parameter` (1 to 4), or every call
 * when it is 0. Steps 4 to 2 are undone and step 7 never runs. */
static bool case_refused(Session *s)
{
    if (s->parameter == 0) {
        s->alloc.refuse_all = true;
    } else {
        s->alloc.refuse_call = (size_t)s->parameter;
    }
    CHECK(create_fails(s, OVD_ERROR_ALLOCATION));
    const size_t calls = s->parameter == 0 ? 1 : (size_t)s->parameter;
    CHECK(s->alloc.allocate_calls == calls);
    CHECK(s->alloc.release_calls == calls - 1);
    return expect_output(&s->pty, LITERAL(""), "refused allocation");
}

/* Step 5: a size that reads 0 in either dimension (R25). */
static bool case_zero_size(Session *s)
{
    CHECK(create_fails(s, OVD_ERROR_PLATFORM));
    CHECK(s->alloc.allocate_calls == 0);
    return expect_output(&s->pty, LITERAL(""), "zero size");
}

/* Step 7: stdout is the slave opened read-only, so steps 1 and 5 succeed
 * and every write fails (POSIX write: EBADF). Step 6 ran and was undone,
 * then steps 4 to 2. */
static bool case_enter_fails(Session *s)
{
    CHECK(create_fails(s, OVD_ERROR_IO));
    CHECK(s->alloc.allocate_calls == 4);
    CHECK(s->alloc.release_calls == 4);
    return expect_output(&s->pty, LITERAL(""), "failed enter");
}

/* Each frame re-reads the size (FR-014). Created at 10x4 without the
 * alternate screen, so the exit sequence shows the last ended frame's
 * rows (R46). */
static bool case_frames(Session *s)
{
    const OvdTerminalConfig c = system_config(s, false);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_PRIMARY), "enter"));
    CHECK(create_twin(s, (OvdSize){ 10, 4 }));

    /* 1: the first frame is a full redraw at the size read at create. */
    CHECK(frame(s, (OvdSize){ 10, 4 }, 0, "frame 1"));
    CHECK(contains(s->capture.bytes, s->capture.length, FULL_REDRAW));

    /* 2: resized between frames. Only the next begin reads it, and that
     * frame is a full redraw at the new size. */
    CHECK(pty_resize(&s->pty, 12, 5));
    CHECK(size_equal(ovd_terminal_size(s->system), (OvdSize){ 10, 4 }));
    CHECK_RESULT(ovd_terminal_set_size(s->headless, (OvdSize){ 12, 5 }), OVD_OK);
    CHECK(frame(s, (OvdSize){ 12, 5 }, 0, "frame 2"));
    CHECK(replay_shows(&s->capture, (OvdSize){ 12, 5 }, "#"));

    /* 3: unchanged, so nothing is written. */
    CHECK(frame(s, (OvdSize){ 12, 5 }, 0, "frame 3"));
    CHECK(s->capture.length == 0);

    /* A size that reads 0 fails the begin and changes nothing (R25). */
    CHECK(pty_resize(&s->pty, 0, 0));
    OvdCanvas *canvas = nullptr;
    CHECK_RESULT(ovd_frame_begin(s->system, &canvas), OVD_ERROR_PLATFORM);
    CHECK(canvas == nullptr);
    CHECK(size_equal(ovd_terminal_size(s->system), (OvdSize){ 12, 5 }));
    CHECK(expect_output(&s->pty, LITERAL(""), "failed begin"));

    /* 4: resized while the frame is open: it ends at the size it began
     * with, as a diff. */
    CHECK(pty_resize(&s->pty, 12, 5));
    CHECK(begin_both(s, (OvdSize){ 12, 5 }, 1));
    CHECK(pty_resize(&s->pty, 7, 3));
    CHECK(end_both(s, "frame 4"));
    CHECK(s->capture.length > 0);
    CHECK(!contains(s->capture.bytes, s->capture.length, FULL_REDRAW));

    /* 5: the next begin reads 7x3: a full redraw. */
    CHECK_RESULT(ovd_terminal_set_size(s->headless, (OvdSize){ 7, 3 }), OVD_OK);
    CHECK(frame(s, (OvdSize){ 7, 3 }, 1, "frame 5"));
    CHECK(contains(s->capture.bytes, s->capture.length, FULL_REDRAW));

    /* The cursor goes to the last ended frame's last row: 3, not the
     * create-time 4. */
    ovd_terminal_destroy(s->system);
    s->system = nullptr;
    CHECK(expect_output(&s->pty, LITERAL(EXIT_AT_ROW(3)), "exit"));
    CHECK(modes_are(&s->pty, false));
    return true;
}

/* The smallest terminal: every element of the frame lands in, or is
 * clipped around, its one cell. */
static bool case_one_by_one(Session *s)
{
    const OvdTerminalConfig c = system_config(s, false);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_PRIMARY), "enter"));
    CHECK(create_twin(s, (OvdSize){ 1, 1 }));
    CHECK(frame(s, (OvdSize){ 1, 1 }, 0, "1x1 frame"));
    CHECK(replay_shows(&s->capture, (OvdSize){ 1, 1 }, "#"));
    ovd_terminal_destroy(s->system);
    s->system = nullptr;
    CHECK(expect_output(&s->pty, LITERAL(EXIT_AT_ROW(1)), "exit"));
    CHECK(modes_are(&s->pty, false));
    return true;
}

/* ---- Input (T114) ---------------------------------------------------- */

/* Input on, on the alternate screen, with all three reports or none.
 * restore_on_exit is off: its handlers are OVD-19's subject, not this
 * test's. */
static OvdTerminalConfig input_config(Session *s, bool reports)
{
    OvdTerminalConfig c = system_config(s, true);
    c.input = true;
    c.mouse = reports;
    c.bracketed_paste = reports;
    c.focus_events = reports;
    c.restore_on_exit = false;
    return c;
}

/* R106's raw mode: the pty's initial modes with exactly these changed. */
static bool raw_modes_are(const Pty *pty)
{
    const tcflag_t local_off = ICANON | ISIG | IEXTEN | ECHO;
    const tcflag_t input_off = IXON | ICRNL | INLCR | IGNCR | ISTRIP;
    struct termios expected = pty->initial;
    expected.c_lflag &= ~local_off;
    expected.c_iflag &= ~input_off;
    expected.c_cc[VMIN] = 0;
    expected.c_cc[VTIME] = 0;
    struct termios now;
    CHECK(tcgetattr(pty->slave, &now) == 0);
    CHECK((now.c_lflag & local_off) == 0);
    CHECK((now.c_iflag & input_off) == 0);
    CHECK(now.c_cc[VMIN] == 0 && now.c_cc[VTIME] == 0);
    CHECK((now.c_oflag & OPOST) == (pty->initial.c_oflag & OPOST));
    if (!modes_equal(&expected, &now)) {
        fprintf(stderr, "raw modes: iflag %#lx lflag %#lx, expected %#lx %#lx\n",
                (unsigned long)now.c_iflag, (unsigned long)now.c_lflag,
                (unsigned long)expected.c_iflag, (unsigned long)expected.c_lflag);
        return false;
    }
    return true;
}

/* SIGWINCH's current action is *expected (handler and flags). */
static bool winch_is(const struct sigaction *expected)
{
    struct sigaction now;
    CHECK(sigaction(SIGWINCH, nullptr, &now) == 0);
    CHECK(now.sa_handler == expected->sa_handler);
    CHECK(now.sa_flags == expected->sa_flags);
    return true;
}

static int64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        return 0;
    }
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static bool wait_kind(Session *s, int32_t timeout_ms, OvdEventKind kind, OvdEvent *event)
{
    OvdEvent got;
    CHECK_RESULT(ovd_input_wait(s->system, timeout_ms, &got), OVD_OK);
    if (got.kind != kind) {
        fprintf(stderr, "wait(%d): event kind %d, expected %d\n", (int)timeout_ms,
                (int)got.kind, (int)kind);
        return false;
    }
    if (event != nullptr) {
        *event = got;
    }
    return true;
}

static bool expect_key(Session *s, OvdKey key, uint32_t codepoint, uint8_t modifiers)
{
    OvdEvent event;
    CHECK(wait_kind(s, 2000, OVD_EVENT_KIND_KEY, &event));
    if (event.key.key != key || event.key.codepoint != codepoint
        || event.key.modifiers != modifiers) {
        fprintf(stderr, "key %d U+%04X mods %d, expected %d U+%04X mods %d\n",
                (int)event.key.key, (unsigned)event.key.codepoint, (int)event.key.modifiers,
                (int)key, (unsigned)codepoint, (int)modifiers);
        return false;
    }
    return true;
}

/* A forever wait for a resize to cols x rows. alarm is the hang guard:
 * SIGALRM's default action ends the test if the wakeup never comes. */
static bool expect_resize(Session *s, int32_t cols, int32_t rows)
{
    OvdEvent event;
    alarm(10);
    const bool got = wait_kind(s, OVD_WAIT_FOREVER, OVD_EVENT_KIND_RESIZE, &event);
    alarm(0);
    CHECK(got);
    if (event.size.cols != cols || event.size.rows != rows) {
        fprintf(stderr, "resize %dx%d, expected %dx%d\n", event.size.cols, event.size.rows, cols,
                rows);
        return false;
    }
    return true;
}

static bool resize_and_signal(Session *s, int32_t cols, int32_t rows)
{
    CHECK(pty_resize(&s->pty, cols, rows));
    CHECK(kill(getpid(), SIGWINCH) == 0);
    return true;
}

/* R106: raw mode while the terminal lives, and every mode back after
 * destroy; the SIGWINCH handler installed and put back (FR-035); feed is
 * headless only. */
static bool case_input_modes(Session *s)
{
    struct sigaction before;
    CHECK(sigaction(SIGWINCH, nullptr, &before) == 0);
    const OvdTerminalConfig c = input_config(s, false);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    /* Create's batch plus the input queue and paste buffer (R114). */
    CHECK(s->alloc.allocate_calls == 6);
    CHECK(raw_modes_are(&s->pty));
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE), "enter"));
    struct sigaction during;
    CHECK(sigaction(SIGWINCH, nullptr, &during) == 0);
    CHECK(during.sa_handler != before.sa_handler);

    CHECK(wait_kind(s, 0, OVD_EVENT_KIND_NONE, nullptr));
    size_t accepted = 7;
    CHECK_RESULT(ovd_input_feed(s->system, "a", 1, &accepted), OVD_ERROR_UNSUPPORTED);
    CHECK(accepted == 7);

    ovd_terminal_destroy(s->system);
    s->system = nullptr;
    CHECK(expect_output(&s->pty, LITERAL(EXIT_ALTERNATE), "exit"));
    CHECK(modes_are(&s->pty, false));
    CHECK(winch_is(&before));
    CHECK(s->alloc.release_calls == 6);
    return true;
}

/* R116: each report on in the enter write, off in reverse after CAN. */
static bool case_input_reports(Session *s)
{
    OvdTerminalConfig c = input_config(s, true);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE REPORTS_ON), "enter, all reports"));
    ovd_terminal_destroy(s->system);
    s->system = nullptr;
    CHECK(expect_output(&s->pty, LITERAL(EXIT_REPORTS_ALTERNATE), "exit, all reports"));

    c = input_config(s, false);
    c.mouse = true;
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE "\x1b[?1002h\x1b[?1006h"),
                        "enter, mouse"));
    ovd_terminal_destroy(s->system);
    s->system = nullptr;
    CHECK(expect_output(&s->pty,
                        LITERAL(CAN "\x1b[0m\x1b[?1006l\x1b[?1002l" EXIT_TAIL "\x1b[?1049l"),
                        "exit, mouse"));

    c = input_config(s, false);
    c.bracketed_paste = true;
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE "\x1b[?2004h"), "enter, paste"));
    ovd_terminal_destroy(s->system);
    s->system = nullptr;
    CHECK(expect_output(&s->pty, LITERAL(CAN "\x1b[0m\x1b[?2004l" EXIT_TAIL "\x1b[?1049l"),
                        "exit, paste"));
    CHECK(modes_are(&s->pty, false));
    return true;
}

/* Bytes written to the master arrive as events, and none is echoed
 * (FR-002, FR-019). 0x03 is Ctrl+C as a key. */
static bool case_input_keys(Session *s)
{
    const OvdTerminalConfig c = input_config(s, false);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE), "enter"));
    CHECK(write_all(s->pty.master, LITERAL("q")));
    CHECK(expect_key(s, OVD_KEY_CHAR, 'q', 0));
    CHECK(write_all(s->pty.master, LITERAL("\x1b[A")));
    CHECK(expect_key(s, OVD_KEY_UP, 0, 0));
    CHECK(write_all(s->pty.master, LITERAL("\x03")));
    CHECK(expect_key(s, OVD_KEY_CHAR, 'c', OVD_MODIFIER_CTRL));
    CHECK(write_all(s->pty.master, LITERAL("ab\r")));
    CHECK(expect_key(s, OVD_KEY_CHAR, 'a', 0));
    CHECK(expect_key(s, OVD_KEY_CHAR, 'b', 0));
    CHECK(expect_key(s, OVD_KEY_ENTER, 0, 0));
    CHECK(wait_kind(s, 0, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(expect_output(&s->pty, LITERAL(""), "no echo"));
    return true;
}

/* R111: a SIGWINCH wakes a forever wait with one resize; two before a
 * wait coalesce; a size that is unchanged or reads as 0 gives none. */
static bool case_input_resize(Session *s)
{
    const OvdTerminalConfig c = input_config(s, false);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE), "enter"));

    CHECK(resize_and_signal(s, 20, 6));
    CHECK(expect_resize(s, 20, 6));
    CHECK(wait_kind(s, 0, OVD_EVENT_KIND_NONE, nullptr));

    CHECK(resize_and_signal(s, 30, 7));
    CHECK(resize_and_signal(s, 31, 8));
    CHECK(expect_resize(s, 31, 8));
    CHECK(wait_kind(s, 0, OVD_EVENT_KIND_NONE, nullptr));

    CHECK(kill(getpid(), SIGWINCH) == 0); /* the size is unchanged */
    CHECK(wait_kind(s, 0, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(resize_and_signal(s, 0, 8)); /* reads as 0 (R25) */
    CHECK(wait_kind(s, 100, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(resize_and_signal(s, 31, 8)); /* back to the last one reported */
    CHECK(wait_kind(s, 0, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(resize_and_signal(s, 12, 5));
    CHECK(expect_resize(s, 12, 5));

    /* The next frame still re-reads the size (FR-017). */
    OvdCanvas *canvas = nullptr;
    CHECK_RESULT(ovd_frame_begin(s->system, &canvas), OVD_OK);
    CHECK(size_equal(ovd_canvas_size(canvas), (OvdSize){ 12, 5 }));
    ovd_frame_abandon(s->system);
    return true;
}

/* SC-005: a byte written while a long wait blocks returns its event
 * within 1 s of being written; a lone ESC becomes Escape no sooner than
 * the escape delay and within the delay plus 1 s. */
static bool case_input_timing(Session *s)
{
    const OvdTerminalConfig c = input_config(s, false);
    CHECK_RESULT(ovd_terminal_create(&c, &s->system), OVD_OK);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE), "enter"));

    constexpr int64_t WRITER_DELAY_MS = 200;
    const int64_t start = now_ms();
    const pid_t writer = fork();
    CHECK(writer >= 0);
    if (writer == 0) {
        const struct timespec delay = { .tv_sec = 0, .tv_nsec = WRITER_DELAY_MS * 1000000 };
        nanosleep(&delay, nullptr);
        _exit(write(s->pty.master, "x", 1) == 1 ? 0 : 1);
    }
    const bool got = expect_key(s, OVD_KEY_CHAR, 'x', 0);
    const int64_t elapsed = now_ms() - start;
    int status = 0;
    CHECK(waitpid(writer, &status, 0) == writer);
    CHECK(got);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (elapsed < WRITER_DELAY_MS - 10 || elapsed > WRITER_DELAY_MS + 1000) {
        fprintf(stderr, "a byte written after %lld ms arrived after %lld ms\n",
                (long long)WRITER_DELAY_MS, (long long)elapsed);
        return false;
    }

    CHECK(write_all(s->pty.master, LITERAL("\x1b")));
    const int64_t escape_start = now_ms();
    alarm(10);
    const bool escaped = wait_kind(s, OVD_WAIT_FOREVER, OVD_EVENT_KIND_KEY, nullptr);
    alarm(0);
    const int64_t escape_elapsed = now_ms() - escape_start;
    CHECK(escaped);
    const int64_t delay = c.escape_delay_ms;
    if (escape_elapsed < delay - 1 || escape_elapsed > delay + 1000) {
        fprintf(stderr, "a lone ESC resolved after %lld ms (delay %lld)\n",
                (long long)escape_elapsed, (long long)delay);
        return false;
    }
    return true;
}

/* R115: input on needs stdin to be a terminal; nothing is changed,
 * written, or allocated. */
static bool case_input_null_stdin(Session *s)
{
    struct sigaction before;
    CHECK(sigaction(SIGWINCH, nullptr, &before) == 0);
    const OvdTerminalConfig c = input_config(s, true);
    OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
    OvdTerminal *out = sentinel;
    CHECK_RESULT(ovd_terminal_create(&c, &out), OVD_ERROR_NOT_A_TERMINAL);
    CHECK(out == sentinel);
    CHECK(s->alloc.allocate_calls == 0);
    CHECK(modes_are(&s->pty, false));
    CHECK(winch_is(&before));
    return expect_output(&s->pty, LITERAL(""), "stdin not a terminal");
}

static volatile sig_atomic_t application_winches;

static void application_winch(int)
{
    application_winches = application_winches + 1;
}

/* FR-032: a SIGWINCH handler installed before create still runs while the
 * terminal lives, and is back after destroy. */
static bool case_input_app_handler(Session *s)
{
    struct sigaction application = { .sa_handler = application_winch };
    sigemptyset(&application.sa_mask);
    struct sigaction original;
    CHECK(sigaction(SIGWINCH, &application, &original) == 0);
    struct sigaction installed;
    CHECK(sigaction(SIGWINCH, nullptr, &installed) == 0);

    const OvdTerminalConfig c = input_config(s, false);
    bool ok = ovd_terminal_create(&c, &s->system) == OVD_OK;
    if (ok) {
        application_winches = 0;
        ok = kill(getpid(), SIGWINCH) == 0 && application_winches == 1
             && wait_kind(s, 0, OVD_EVENT_KIND_NONE, nullptr) && resize_and_signal(s, 20, 6)
             && application_winches == 2 && expect_resize(s, 20, 6);
        ovd_terminal_destroy(s->system);
        s->system = nullptr;
        ok = ok && winch_is(&installed);
    }
    sigaction(SIGWINCH, &original, nullptr);
    CHECK(ok);
    CHECK(expect_output(&s->pty, LITERAL(ENTER_ALTERNATE EXIT_ALTERNATE), "enter and exit"));
    CHECK(modes_are(&s->pty, false));
    return true;
}

/* Step 6 refused at allocate call `parameter` (1 to 6) with input on:
 * raw mode, echo, and the SIGWINCH handler are all undone. */
static bool case_input_refused(Session *s)
{
    struct sigaction before;
    CHECK(sigaction(SIGWINCH, nullptr, &before) == 0);
    s->alloc.refuse_call = (size_t)s->parameter;
    const OvdTerminalConfig c = input_config(s, true);
    OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
    OvdTerminal *out = sentinel;
    CHECK_RESULT(ovd_terminal_create(&c, &out), OVD_ERROR_ALLOCATION);
    CHECK(out == sentinel);
    CHECK(s->alloc.allocate_calls == (size_t)s->parameter);
    CHECK(s->alloc.release_calls == (size_t)s->parameter - 1);
    CHECK(modes_are(&s->pty, false));
    CHECK(winch_is(&before));
    return expect_output(&s->pty, LITERAL(""), "refused allocation, input on");
}

/* Step 7 fails with input on: everything create did is undone. */
static bool case_input_enter_fails(Session *s)
{
    struct sigaction before;
    CHECK(sigaction(SIGWINCH, nullptr, &before) == 0);
    const OvdTerminalConfig c = input_config(s, true);
    OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
    OvdTerminal *out = sentinel;
    CHECK_RESULT(ovd_terminal_create(&c, &out), OVD_ERROR_IO);
    CHECK(out == sentinel);
    CHECK(s->alloc.allocate_calls == 6 && s->alloc.release_calls == 6);
    CHECK(modes_are(&s->pty, false));
    CHECK(winch_is(&before));
    return expect_output(&s->pty, LITERAL(""), "failed enter, input on");
}

/* ---- Driver ---------------------------------------------------------- */

typedef bool (*Case)(Session *s);

/* Runs one case on a fresh pty of the given size, then cleans up, pass
 * or fail: destroys any terminal left, restores the test's descriptors,
 * and fails if a block is still live or the allocator contract was
 * broken. */
static bool run(const char *name, int parameter, OvdSize size, Placement placement, Case body)
{
    static Session s;
    s = (Session){ .parameter = parameter };
    test_alloc_init(&s.alloc);
    s.allocator = test_alloc_allocator(&s.alloc);
    bool ok = pty_open(&s.pty, size, placement);
    if (ok) {
        ok = body(&s);
        ovd_terminal_destroy(s.system);
        ovd_terminal_destroy(s.headless);
        pty_close(&s.pty);
    }
    if (s.alloc.live_blocks != 0 || s.alloc.violations != 0) {
        fprintf(stderr, "%zu blocks live, %zu allocator contract violations\n",
                s.alloc.live_blocks, s.alloc.violations);
        test_alloc_release_all(&s.alloc);
        ok = false;
    }
    fprintf(stderr, "%s (%d, %dx%d): %s\n", name, parameter, size.cols, size.rows,
            ok ? "ok" : "FAILED");
    return ok;
}

/* A closed fd 0, 1, or 2 would let a pty land on it: point any closed
 * one at /dev/null first. */
static bool standard_fds_open(void)
{
    for (int fd = 0; fd <= 2; ++fd) {
        if (fcntl(fd, F_GETFD) == -1 && open("/dev/null", O_RDWR) != fd) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    if (!standard_fds_open()) {
        return EXIT_FAILURE;
    }
    const OvdSize size = { 10, 4 };
    bool ok = true;
    ok = run("enter and exit, alternate screen", 0, size, PLACE_BOTH, case_alternate) && ok;
    ok = run("enter and exit, primary screen", 0, size, PLACE_BOTH, case_primary) && ok;
    for (int call = 0; call <= 4; ++call) {
        ok = run("allocation refused (0: all)", call, size, PLACE_BOTH, case_refused) && ok;
    }
    ok = run("size reads 0", 0, (OvdSize){ 0, 0 }, PLACE_BOTH, case_zero_size) && ok;
    ok = run("size reads 0", 0, (OvdSize){ 0, 4 }, PLACE_BOTH, case_zero_size) && ok;
    ok = run("size reads 0", 0, (OvdSize){ 10, 0 }, PLACE_BOTH, case_zero_size) && ok;
    ok = run("enter write fails", 0, size, PLACE_READ_ONLY_STDOUT, case_enter_fails) && ok;
    ok = run("frames and resizes", 0, size, PLACE_BOTH, case_frames) && ok;
    ok = run("1x1", 0, (OvdSize){ 1, 1 }, PLACE_BOTH, case_one_by_one) && ok;

    /* Input on (T114). */
    ok = run("input: raw mode and restore", 0, size, PLACE_BOTH, case_input_modes) && ok;
    ok = run("input: report bytes", 0, size, PLACE_BOTH, case_input_reports) && ok;
    ok = run("input: keys", 0, size, PLACE_BOTH, case_input_keys) && ok;
    ok = run("input: resize", 0, size, PLACE_BOTH, case_input_resize) && ok;
    ok = run("input: timing", 0, size, PLACE_BOTH, case_input_timing) && ok;
    ok = run("input: stdin not a terminal", 0, size, PLACE_NULL_STDIN, case_input_null_stdin)
         && ok;
    ok = run("input: application SIGWINCH handler", 0, size, PLACE_BOTH, case_input_app_handler)
         && ok;
    for (int call = 1; call <= 6; ++call) {
        ok = run("input: allocation refused", call, size, PLACE_BOTH, case_input_refused) && ok;
    }
    ok = run("input: enter write fails", 0, size, PLACE_READ_ONLY_STDOUT, case_input_enter_fails)
         && ok;
    if (!ok) {
        fprintf(stderr, "test_system_pty: FAILED\n");
        return EXIT_FAILURE;
    }
    fprintf(stderr, "test_system_pty: all checks passed\n");
    return EXIT_SUCCESS;
}
