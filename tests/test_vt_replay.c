/* Tests of the replay interpreter and screen model (tests/support). */
#include "vt_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

/* Fails with both screens' text and the first difference. */
#define CHECK_SCREEN(expected, actual) \
    do { \
        char message_[512]; \
        if (!test_screen_equal((expected), (actual), message_, sizeof message_)) { \
            char dump_[2048]; \
            fprintf(stderr, "%s:%d: screens differ: %s\n", __FILE__, __LINE__, message_); \
            test_screen_dump((actual), dump_, sizeof dump_); \
            fprintf(stderr, "actual:\n%s", dump_); \
            return false; \
        } \
    } while (false)

/* The interpreter's own classifier: just the code points these tests use. */
static TestWidth classify(uint32_t code_point)
{
    switch (code_point) {
    case 0x4E16: /* 世 */
    case 0x754C: /* 界 */
    case 0x1F469: /* woman */
    case 0x1F467: /* girl */
        return TEST_WIDTH_WIDE;
    case 0x0301: /* combining acute */
    case 0xFE0F: /* VS16 */
        return TEST_WIDTH_EXTEND;
    default:
        return TEST_WIDTH_NARROW;
    }
}

static bool feed(VtReplay *vt, const char *bytes)
{
    return vt_replay_feed(vt, bytes, strlen(bytes));
}

/* Feeding bytes fails, and the error names expected_fragment. */
static bool rejects(int32_t cols, int32_t rows, const char *bytes, size_t length,
                    const char *expected_fragment)
{
    VtReplay *vt = vt_replay_create(cols, rows, classify);
    if (vt == nullptr) {
        fprintf(stderr, "create failed\n");
        return false;
    }
    const bool accepted = vt_replay_feed(vt, bytes, length);
    const char *error = vt_replay_error(vt);
    const bool ok = !accepted && strstr(error, expected_fragment) != nullptr;
    if (!ok) {
        fprintf(stderr, "expected rejection naming \"%s\"; accepted=%d error=\"%s\"\n",
                expected_fragment, accepted, error);
    }
    vt_replay_destroy(vt);
    return ok;
}

#define CHECK_REJECTS(cols, rows, literal, fragment) \
    CHECK(rejects((cols), (rows), (literal), sizeof(literal) - 1, (fragment)))

static TestScreen *rows_screen(int32_t cols, int32_t rows, const char *const text[],
                               const char *const styles[], const TestStyleKey legend[],
                               size_t legend_count)
{
    char error[256] = "";
    TestScreen *screen = test_screen_from_rows(cols, rows, classify, text, styles, legend,
                                               legend_count, error, sizeof error);
    if (screen == nullptr) {
        fprintf(stderr, "from_rows failed: %s\n", error);
    }
    return screen;
}

static bool test_blank_and_size(void)
{
    CHECK(vt_replay_create(0, 24, classify) == nullptr);
    CHECK(vt_replay_create(80, -1, classify) == nullptr);
    CHECK(vt_replay_create(80, 24, nullptr) == nullptr);
    CHECK(vt_replay_create(INT32_MAX, INT32_MAX, classify) == nullptr);
    CHECK(test_screen_create(0, 0) == nullptr);

    VtReplay *vt = vt_replay_create(4, 2, classify);
    CHECK(vt != nullptr);
    CHECK(vt_replay_feed(vt, "", 0));
    const char *const text[] = { "", "" };
    TestScreen *expected = rows_screen(4, 2, text, nullptr, nullptr, 0);
    CHECK(expected != nullptr);
    CHECK_SCREEN(expected, vt_replay_screen(vt));
    const VtReplayState state = vt_replay_state(vt);
    CHECK(!state.cursor_known);
    CHECK(state.cursor_visible && state.autowrap);
    CHECK(!state.alternate_screen && !state.synchronized);
    CHECK(strcmp(vt_replay_error(vt), "") == 0);
    test_screen_destroy(expected);
    vt_replay_destroy(vt);
    return true;
}

/* Spec US1 scenario 1: text in Ignition Orange at (2, 1). */
static bool test_cup_and_rgb_text(void)
{
    VtReplay *vt = vt_replay_create(30, 3, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[2;3H\x1b[38;2;255;145;56mEvery frame matters.\x1b[0m"));
    const VtReplayState state = vt_replay_state(vt);
    CHECK(state.cursor_known && state.x == 22 && state.y == 1 && !state.pending_wrap);

    const TestStyleKey legend[] = {
        { 'o', { .fg = ovd_color_rgb(255, 145, 56), .bg = OVD_COLOR_DEFAULT } },
    };
    const char *const text[] = { "", "  Every frame matters.", "" };
    const char *const styles[] = { "", "..oooooooooooooooooooo", "" };
    TestScreen *expected = rows_screen(30, 3, text, styles, legend, 1);
    CHECK(expected != nullptr);
    CHECK_SCREEN(expected, vt_replay_screen(vt));

    /* A wrong style is a difference that names the cell. */
    const char *const plain[] = { "", "  Every frame matters.", "" };
    TestScreen *unstyled = rows_screen(30, 3, plain, nullptr, nullptr, 0);
    CHECK(unstyled != nullptr);
    char message[512];
    CHECK(!test_screen_equal(unstyled, vt_replay_screen(vt), message, sizeof message));
    CHECK(strstr(message, "(2, 1)") != nullptr);
    test_screen_destroy(unstyled);
    test_screen_destroy(expected);
    vt_replay_destroy(vt);
    return true;
}

static bool test_cup_validation(void)
{
    CHECK_REJECTS(10, 5, "\x1b[0;1H", "\\x1b[0;1H");
    CHECK_REJECTS(10, 5, "\x1b[1;0H", "\\x1b[1;0H");
    CHECK_REJECTS(10, 5, "\x1b[6;1H", "\\x1b[6;1H");
    CHECK_REJECTS(10, 5, "\x1b[1;11H", "\\x1b[1;11H");
    CHECK_REJECTS(10, 5, "\x1b[H", "\\x1b[H");
    CHECK_REJECTS(10, 5, "\x1b[3H", "\\x1b[3H");
    CHECK_REJECTS(10, 5, "\x1b[01;1H", "\\x1b[01;1H");
    CHECK_REJECTS(10, 5, "\x1b[1;1;1H", "\\x1b[1;1;1H");
    CHECK_REJECTS(10, 5, "\x1b[99999999;1H", "\\x1b[99999999;1H");

    VtReplay *vt = vt_replay_create(10, 5, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[5;10H"));
    const VtReplayState state = vt_replay_state(vt);
    CHECK(state.x == 9 && state.y == 4);
    vt_replay_destroy(vt);

    /* Parameters have no digit limit: six-digit coordinates on both axes. */
    VtReplay *wide = vt_replay_create(100001, 1, classify);
    CHECK(wide != nullptr);
    CHECK(feed(wide, "\x1b[1;100000Hx"));
    const VtReplayState wide_state = vt_replay_state(wide);
    CHECK(wide_state.x == 100000 && wide_state.y == 0 && !wide_state.pending_wrap);
    CHECK(strcmp(test_screen_cell(vt_replay_screen(wide), 99999, 0)->text, "x") == 0);
    CHECK(feed(wide, "\x1b[1;100001H"));
    CHECK(vt_replay_state(wide).x == 100000);
    vt_replay_destroy(wide);
    VtReplay *tall = vt_replay_create(1, 100001, classify);
    CHECK(tall != nullptr);
    CHECK(feed(tall, "\x1b[100000;1Hy"));
    const VtReplayState tall_state = vt_replay_state(tall);
    CHECK(tall_state.x == 0 && tall_state.y == 99999 && tall_state.pending_wrap);
    CHECK(strcmp(test_screen_cell(vt_replay_screen(tall), 0, 99999)->text, "y") == 0);
    CHECK(feed(tall, "\x1b[100001;1H"));
    CHECK(vt_replay_state(tall).y == 100000);
    vt_replay_destroy(tall);

    /* A value past UINT32_MAX is rejected while parsing, never wrapped into
     * range (4294967297 would wrap to 1). UINT32_MAX itself parses, then
     * fails CUP's range check. */
    CHECK_REJECTS(10, 5, "\x1b[4294967297;1H", "parameters");
    CHECK_REJECTS(10, 5, "\x1b[1;4294967297H", "parameters");
    CHECK_REJECTS(10, 5, "\x1b[4294967296;1H", "parameters");
    CHECK_REJECTS(10, 5, "\x1b[99999999999999999999;1H", "parameters");
    CHECK_REJECTS(10, 5, "\x1b[4294967295;1H", "inside the screen");
    CHECK_REJECTS(10, 5, "\x1b[1;2147483648H", "inside the screen");
    return true;
}

static bool test_sgr(void)
{
    VtReplay *vt = vt_replay_create(20, 1, classify);
    CHECK(vt != nullptr);
    /* Attributes accumulate as deltas; 0 resets. */
    CHECK(feed(vt, "\x1b[1;1H\x1b[1ma\x1b[4mb\x1b[0;2;3;7;9mc\x1b[0md"));
    /* Colors: basic, bright, 256, RGB, defaults. */
    CHECK(feed(vt, "\x1b[31;42me\x1b[97;104mf\x1b[38;5;200;48;5;17mg"
                   "\x1b[38;2;1;2;3;48;2;4;5;6mh\x1b[39mi\x1b[49mj\x1b[0m"));

    const OvdStyle def = OVD_STYLE_DEFAULT;
    OvdStyle bold = def;
    bold.attrs = OVD_ATTR_BOLD;
    OvdStyle bold_under = def;
    bold_under.attrs = OVD_ATTR_BOLD | OVD_ATTR_UNDERLINE;
    OvdStyle mixed = def;
    mixed.attrs = OVD_ATTR_DIM | OVD_ATTR_ITALIC | OVD_ATTR_REVERSE | OVD_ATTR_STRIKETHROUGH;
    const OvdStyle basic = { .fg = ovd_color_indexed(1), .bg = ovd_color_indexed(2) };
    const OvdStyle bright = { .fg = ovd_color_indexed(15), .bg = ovd_color_indexed(12) };
    const OvdStyle palette = { .fg = ovd_color_indexed(200), .bg = ovd_color_indexed(17) };
    const OvdStyle rgb = { .fg = ovd_color_rgb(1, 2, 3), .bg = ovd_color_rgb(4, 5, 6) };
    const OvdStyle fg_default = { .fg = OVD_COLOR_DEFAULT, .bg = ovd_color_rgb(4, 5, 6) };
    const TestStyleKey legend[] = {
        { 'B', bold }, { 'U', bold_under }, { 'M', mixed }, { '1', basic },
        { '2', bright }, { '3', palette }, { '4', rgb }, { '5', fg_default },
    };
    const char *const text[] = { "abcdefghij" };
    const char *const styles[] = { "BUM.12345." };
    TestScreen *expected = rows_screen(20, 1, text, styles, legend,
                                       sizeof legend / sizeof legend[0]);
    CHECK(expected != nullptr);
    CHECK_SCREEN(expected, vt_replay_screen(vt));
    test_screen_destroy(expected);
    vt_replay_destroy(vt);

    /* Outside the subset: off codes, empty reset, colons, ranges, other codes. */
    CHECK_REJECTS(5, 1, "\x1b[22m", "\\x1b[22m");
    CHECK_REJECTS(5, 1, "\x1b[24m", "\\x1b[24m");
    CHECK_REJECTS(5, 1, "\x1b[5m", "\\x1b[5m");
    CHECK_REJECTS(5, 1, "\x1b[m", "\\x1b[m");
    CHECK_REJECTS(5, 1, "\x1b[1;;3m", "\\x1b[1;;3m");
    CHECK_REJECTS(5, 1, "\x1b[4:3m", "\\x1b[4:3m");
    CHECK_REJECTS(5, 1, "\x1b[38;5;256m", "\\x1b[38;5;256m");
    CHECK_REJECTS(5, 1, "\x1b[38;2;1;2m", "\\x1b[38;2;1;2m");
    CHECK_REJECTS(5, 1, "\x1b[38;3;1m", "\\x1b[38;3;1m");
    CHECK_REJECTS(5, 1, "\x1b[38m", "\\x1b[38m");
    CHECK_REJECTS(5, 1, "\x1b[38;2;300;0;0m", "\\x1b[38;2;300;0;0m");
    CHECK_REJECTS(5, 1, "\x1b[29m", "\\x1b[29m");
    CHECK_REJECTS(5, 1, "\x1b[98m", "\\x1b[98m");
    CHECK_REJECTS(5, 1, "\x1b[108m", "\\x1b[108m");
    /* Values that would wrap to 1 (bold, palette 1) are rejected, not wrapped. */
    CHECK_REJECTS(5, 1, "\x1b[4294967297m", "\\x1b[4294967297m");
    CHECK_REJECTS(5, 1, "\x1b[38;5;4294967297m", "\\x1b[38;5;4294967297m");
    return true;
}

static bool test_wide_glyphs(void)
{
    VtReplay *vt = vt_replay_create(6, 2, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[1;1H世界x"));
    VtReplayState state = vt_replay_state(vt);
    CHECK(state.x == 5 && !state.pending_wrap);
    const TestCell *lead = test_screen_cell(vt_replay_screen(vt), 0, 0);
    const TestCell *tail = test_screen_cell(vt_replay_screen(vt), 1, 0);
    CHECK(lead != nullptr && lead->width == 2 && strcmp(lead->text, "世") == 0);
    CHECK(tail != nullptr && tail->width == 0 && tail->length == 0);

    /* A wide glyph ending in the last column leaves the cursor pending. */
    CHECK(feed(vt, "\x1b[2;5H世"));
    state = vt_replay_state(vt);
    CHECK(state.x == 5 && state.y == 1 && state.pending_wrap);

    const char *const text[] = { "世界x", "    世" };
    TestScreen *expected = rows_screen(6, 2, text, nullptr, nullptr, 0);
    CHECK(expected != nullptr);
    CHECK_SCREEN(expected, vt_replay_screen(vt));

    /* Splitting a wide glyph leaves the other half indeterminate. */
    CHECK(feed(vt, "\x1b[1;2Hy"));
    char message[512];
    const char *const split_text[] = { " y界x", "    世" };
    TestScreen *split = rows_screen(6, 2, split_text, nullptr, nullptr, 0);
    CHECK(split != nullptr);
    CHECK(!test_screen_equal(split, vt_replay_screen(vt), message, sizeof message));
    CHECK(strstr(message, "(0, 0)") != nullptr && strstr(message, "indeterminate") != nullptr);
    /* Overwriting the orphan makes the screen determinate again. */
    CHECK(feed(vt, "\x1b[1;1Hz"));
    const char *const fixed_text[] = { "zy界x", "    世" };
    TestScreen *fixed = rows_screen(6, 2, fixed_text, nullptr, nullptr, 0);
    CHECK(fixed != nullptr);
    CHECK_SCREEN(fixed, vt_replay_screen(vt));
    /* The left half of 界 at column 2: its right half becomes indeterminate. */
    CHECK(feed(vt, "\x1b[1;3Hq"));
    CHECK(test_screen_cell(vt_replay_screen(vt), 3, 0)->indeterminate);

    char dump[256];
    test_screen_dump(vt_replay_screen(vt), dump, sizeof dump);
    CHECK(strcmp(dump, "zyq?x \n    世\n") == 0);

    test_screen_destroy(fixed);
    test_screen_destroy(split);
    test_screen_destroy(expected);
    vt_replay_destroy(vt);

    /* A wide glyph that starts in the last column would wrap. */
    CHECK_REJECTS(6, 1, "\x1b[1;6H世", "last column");
    CHECK_REJECTS(1, 1, "\x1b[1;1H世", "last column");
    return true;
}

static bool test_pending_wrap(void)
{
    VtReplay *vt = vt_replay_create(3, 2, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[1;1Habc"));
    VtReplayState state = vt_replay_state(vt);
    CHECK(state.x == 2 && state.pending_wrap);
    /* SGR does not move the cursor; a CUP clears the pending state. */
    CHECK(feed(vt, "\x1b[1m\x1b[0m\x1b[2;3Hd"));
    state = vt_replay_state(vt);
    CHECK(state.x == 2 && state.y == 1 && state.pending_wrap);
    vt_replay_destroy(vt);

    CHECK_REJECTS(3, 1, "\x1b[1;1Habcd", "pending-wrap");
    /* Even with autowrap off: output must render the same either way (R33). */
    CHECK_REJECTS(3, 1, "\x1b[?7l\x1b[1;1Habcd", "pending-wrap");
    CHECK_REJECTS(1, 1, "\x1b[1;1Hab", "pending-wrap");
    /* No C0 control is accepted, so nothing can scroll. */
    CHECK_REJECTS(3, 2, "\x1b[1;1Ha\n", "\\x0a");
    CHECK_REJECTS(3, 2, "\x1b[1;1Ha\r", "\\x0d");
    /* Printing before any CUP: the cursor is unknown. */
    CHECK_REJECTS(3, 1, "a", "cursor");
    /* A 1x1 screen works. */
    VtReplay *tiny = vt_replay_create(1, 1, classify);
    CHECK(tiny != nullptr);
    CHECK(feed(tiny, "\x1b[1;1Hq"));
    CHECK(strcmp(test_screen_cell(vt_replay_screen(tiny), 0, 0)->text, "q") == 0);
    vt_replay_destroy(tiny);
    return true;
}

static bool test_erase(void)
{
    VtReplay *vt = vt_replay_create(4, 2, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[1;1H\x1b[1mab世\x1b[0m\x1b[2J"));
    const char *const blank[] = { "", "" };
    TestScreen *expected = rows_screen(4, 2, blank, nullptr, nullptr, 0);
    CHECK(expected != nullptr);
    CHECK_SCREEN(expected, vt_replay_screen(vt));
    /* ED leaves the cursor and the pending-wrap state where they were. */
    const VtReplayState state = vt_replay_state(vt);
    CHECK(state.cursor_known && state.x == 3 && state.pending_wrap);
    test_screen_destroy(expected);
    vt_replay_destroy(vt);

    CHECK_REJECTS(4, 1, "\x1b[J", "\\x1b[J");
    CHECK_REJECTS(4, 1, "\x1b[0J", "\\x1b[0J");
    CHECK_REJECTS(4, 1, "\x1b[3J", "\\x1b[3J");
    CHECK_REJECTS(4, 1, "\x1b[2K", "\\x1b[2K");
    /* The erase color is terminal-dependent unless SGR is reset first. */
    CHECK_REJECTS(4, 1, "\x1b[44m\x1b[2J", "default SGR");
    return true;
}

static bool test_modes(void)
{
    /* Every sequence, private modes included, ends a grapheme. */
    int extender_failures = 0;
    const char *const modes[] = { "?25l", "?7l", "?1049h", "?1049l", "?2026h" };
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        char bytes[32];
        const int length = snprintf(bytes, sizeof bytes, "\x1b[1;1HA\x1b[%s\xcc\x81", modes[i]);
        if (!rejects(4, 1, bytes, (size_t)length, "extender")) {
            fprintf(stderr, "  after %s\n", modes[i]);
            ++extender_failures;
        }
    }
    CHECK(extender_failures == 0);

    VtReplay *vt = vt_replay_create(4, 2, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[?25l\x1b[?7l\x1b[?2026h"));
    VtReplayState state = vt_replay_state(vt);
    CHECK(!state.cursor_visible && !state.autowrap && state.synchronized);
    CHECK(feed(vt, "\x1b[?2026l\x1b[?25h\x1b[?7h"));
    state = vt_replay_state(vt);
    CHECK(state.cursor_visible && state.autowrap && !state.synchronized);

    /* 1049: entering clears the model; leaving forgets it. */
    CHECK(feed(vt, "\x1b[1;1Hab\x1b[?1049h"));
    state = vt_replay_state(vt);
    CHECK(state.alternate_screen && !state.cursor_known);
    const char *const blank[] = { "", "" };
    TestScreen *expected = rows_screen(4, 2, blank, nullptr, nullptr, 0);
    CHECK(expected != nullptr);
    CHECK_SCREEN(expected, vt_replay_screen(vt));
    CHECK(feed(vt, "\x1b[?1049l"));
    state = vt_replay_state(vt);
    CHECK(!state.alternate_screen && !state.cursor_known);
    CHECK(test_screen_cell(vt_replay_screen(vt), 0, 0)->indeterminate);
    test_screen_destroy(expected);
    vt_replay_destroy(vt);

    CHECK_REJECTS(4, 1, "\x1b[?2026h\x1b[?2026h", "2026");
    CHECK_REJECTS(4, 1, "\x1b[?2026l", "2026");
    CHECK_REJECTS(4, 1, "\x1b[?1h", "\\x1b[?1h");
    CHECK_REJECTS(4, 1, "\x1b[?25;7l", "\\x1b[?25;7l");
    CHECK_REJECTS(4, 1, "\x1b[?25", "truncated");
    CHECK_REJECTS(4, 1, "\x1b[?25x", "\\x1b[?25x");
    CHECK_REJECTS(4, 1, "\x1b[25h", "\\x1b[25h");
    /* 4294967321 would wrap to 25 (DECTCEM). */
    CHECK_REJECTS(4, 1, "\x1b[?4294967321l", "\\x1b[?4294967321l");
    /* Entering the alternate screen forgets the cursor (strict side, R38). */
    CHECK_REJECTS(4, 1, "\x1b[1;2H\x1b[?1049hB", "cursor");
    return true;
}

static bool test_unknown_sequences(void)
{
    CHECK_REJECTS(4, 1, "\x1b" "7", "\\x1b7");
    CHECK_REJECTS(4, 1, "\x1b]0;title\x07", "\\x1b]");
    CHECK_REJECTS(4, 1, "\x1b[1;4r", "\\x1b[1;4r");
    CHECK_REJECTS(4, 1, "\x1b[2A", "\\x1b[2A");
    CHECK_REJECTS(4, 1, "\x1b[>c", "\\x1b[>c");
    CHECK_REJECTS(4, 1, "\x1b[ q", "\\x1b[ q");
    CHECK_REJECTS(4, 1, "\x1b", "truncated");
    CHECK_REJECTS(4, 1, "\x1b[", "truncated");
    CHECK_REJECTS(4, 1, "\x1b[1;1", "truncated");
    CHECK_REJECTS(4, 1, "\x07", "\\x07");
    CHECK_REJECTS(4, 1, "\x1b[1;1H\x7f", "\\x7f");
    /* C1 CSI as UTF-8 (U+009B), invalid UTF-8, a raw 8-bit CSI. */
    CHECK_REJECTS(4, 1, "\x1b[1;1H\xc2\x9b", "\\xc2\\x9b");
    CHECK_REJECTS(4, 1, "\x1b[1;1H\x9b", "\\x9b");
    CHECK_REJECTS(4, 1, "\x1b[1;1H\xed\xa0\x80", "\\xed");
    CHECK_REJECTS(4, 1, "\x1b[1;1H\xf0\x9f\x98", "\\xf0");
    CHECK_REJECTS(4, 1, "\x1b[1;1H\xc0\xaf", "\\xc0");
    /* Code points the canvas never holds (R17). */
    CHECK_REJECTS(4, 1, "\x1b[1;1H\xe2\x80\xa8", "U+2028");
    CHECK_REJECTS(4, 1, "\x1b[1;1H\xe2\x80\xae", "U+202E");

    /* The offset counts across feeds, and errors are sticky. */
    VtReplay *vt = vt_replay_create(4, 1, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[1;1Hab"));
    CHECK(!feed(vt, "c\x1b[5n"));
    CHECK(strstr(vt_replay_error(vt), "byte 9") != nullptr);
    char first[256];
    snprintf(first, sizeof first, "%s", vt_replay_error(vt));
    CHECK(!feed(vt, "\x1b[1;1Hz"));
    CHECK(strcmp(first, vt_replay_error(vt)) == 0);
    CHECK(strcmp(test_screen_cell(vt_replay_screen(vt), 0, 0)->text, "a") == 0);
    vt_replay_destroy(vt);

    /* nullptr bytes with a nonzero length is a recorded, sticky error. */
    VtReplay *null_feed = vt_replay_create(4, 1, classify);
    CHECK(null_feed != nullptr);
    CHECK(vt_replay_feed(null_feed, nullptr, 0));
    CHECK(!vt_replay_feed(null_feed, nullptr, 3));
    CHECK(strstr(vt_replay_error(null_feed), "nullptr") != nullptr);
    CHECK(!feed(null_feed, "\x1b[1;1Hz"));
    vt_replay_destroy(null_feed);
    return true;
}

static bool test_extenders(void)
{
    VtReplay *vt = vt_replay_create(6, 1, classify);
    CHECK(vt != nullptr);
    /* e + combining acute; a ZWJ family emoji (11 bytes) is one wide grapheme. */
    CHECK(feed(vt, "\x1b[1;1He\xcc\x81\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7x"));
    const TestScreen *screen = vt_replay_screen(vt);
    CHECK(strcmp(test_screen_cell(screen, 0, 0)->text, "e\xcc\x81") == 0);
    const TestCell *family = test_screen_cell(screen, 1, 0);
    CHECK(family->width == 2 && family->length == 11);
    CHECK(strcmp(test_screen_cell(screen, 3, 0)->text, "x") == 0);
    /* An extender after a glyph in the last column still attaches to it. */
    CHECK(feed(vt, "\x1b[1;6Hq\xef\xb8\x8f"));
    CHECK(strcmp(test_screen_cell(screen, 5, 0)->text, "q\xef\xb8\x8f") == 0);
    vt_replay_destroy(vt);

    /* An extender with nothing printed just before it in this write. */
    CHECK_REJECTS(4, 1, "\x1b[1;1H\xcc\x81", "extender");
    CHECK_REJECTS(4, 1, "\x1b[1;1Ha\x1b[1m\xcc\x81", "extender");
    /* A grapheme may end in ZWJ (R15): the end of the write ends it. */
    VtReplay *open = vt_replay_create(4, 1, classify);
    CHECK(open != nullptr);
    CHECK(feed(open, "\x1b[1;1Ha\xe2\x80\x8d"));
    CHECK(strcmp(test_screen_cell(vt_replay_screen(open), 0, 0)->text, "a\xe2\x80\x8d") == 0);
    vt_replay_destroy(open);
    /* Exactly 15 bytes fit in one grapheme; 17 do not. */
    VtReplay *full = vt_replay_create(4, 1, classify);
    CHECK(full != nullptr);
    CHECK(feed(full, "\x1b[1;1Ha\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81"));
    CHECK(test_screen_cell(vt_replay_screen(full), 0, 0)->length == 15);
    vt_replay_destroy(full);
    CHECK_REJECTS(4, 1, "\x1b[1;1Ha\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81",
                  "15 bytes");

    VtReplay *split = vt_replay_create(4, 1, classify);
    CHECK(split != nullptr);
    CHECK(feed(split, "\x1b[1;1Ha"));
    CHECK(!feed(split, "\xcc\x81"));
    CHECK(strstr(vt_replay_error(split), "extender") != nullptr);
    vt_replay_destroy(split);
    return true;
}

/* R15 allows a cell whose grapheme ends in ZWJ: the end of a text call ends
 * it. The oracle accepts that cell, and the next glyph must follow a CUP,
 * because whether a terminal joins the two is terminal-dependent (R38). */
static bool test_zwj_final(void)
{
    OvdStyle bold = OVD_STYLE_DEFAULT;
    bold.attrs = OVD_ATTR_BOLD;

    /* A frame whose last update ends in ZWJ: the closing reset ends the
     * grapheme, and an expected row may end in ZWJ too. */
    VtReplay *vt = vt_replay_create(4, 1, classify);
    CHECK(vt != nullptr);
    CHECK(feed(vt, "\x1b[1;1Hba\xe2\x80\x8d\x1b[0m"));
    const char *const text[] = { "ba\xe2\x80\x8d" };
    TestScreen *expected = rows_screen(4, 1, text, nullptr, nullptr, 0);
    CHECK(expected != nullptr);
    CHECK(test_screen_cell(expected, 1, 0)->length == 4);
    CHECK_SCREEN(expected, vt_replay_screen(vt));
    /* The next glyph needs a CUP first, even in a later write. */
    CHECK(!feed(vt, "c"));
    CHECK(strstr(vt_replay_error(vt), "no CUP") != nullptr);
    test_screen_destroy(expected);
    vt_replay_destroy(vt);

    /* With a CUP between them: two cells, in one write or across two. */
    TestScreen *two = test_screen_create(4, 1);
    CHECK(two != nullptr);
    test_screen_put(two, 0, 0, "a\xe2\x80\x8d", 4, 1, bold);
    test_screen_put(two, 1, 0, "b", 1, 1, bold);
    VtReplay *frame = vt_replay_create(4, 1, classify);
    CHECK(frame != nullptr);
    CHECK(feed(frame, "\x1b[?2026h\x1b[0m\x1b[2J\x1b[1;1H\x1b[1ma\xe2\x80\x8d\x1b[1;2Hb"
                      "\x1b[0m\x1b[?2026l"));
    CHECK_SCREEN(two, vt_replay_screen(frame));
    vt_replay_destroy(frame);
    VtReplay *split = vt_replay_create(4, 1, classify);
    CHECK(split != nullptr);
    CHECK(feed(split, "\x1b[1;1H\x1b[1ma\xe2\x80\x8d"));
    CHECK(feed(split, "\x1b[1;2Hb\x1b[0m"));
    CHECK_SCREEN(two, vt_replay_screen(split));
    vt_replay_destroy(split);
    test_screen_destroy(two);

    /* The case the rule guards: woman + ZWJ, then girl in the next cell.
     * In one run they are one family grapheme; a CUP keeps two cells. */
    VtReplay *family = vt_replay_create(6, 1, classify);
    CHECK(family != nullptr);
    CHECK(feed(family, "\x1b[1;1H\xf0\x9f\x91\xa9\xe2\x80\x8d\x1b[1;3H\xf0\x9f\x91\xa7"));
    const TestCell *woman = test_screen_cell(vt_replay_screen(family), 0, 0);
    const TestCell *girl = test_screen_cell(vt_replay_screen(family), 2, 0);
    CHECK(woman->width == 2 && woman->length == 7);
    CHECK(girl->width == 2 && strcmp(girl->text, "\xf0\x9f\x91\xa7") == 0);
    vt_replay_destroy(family);

    /* In one run the code point after a ZWJ joins it (R15), as in the canvas. */
    VtReplay *joined = vt_replay_create(4, 1, classify);
    CHECK(joined != nullptr);
    CHECK(feed(joined, "\x1b[1;1Ha\xe2\x80\x8d" "b"));
    CHECK(strcmp(test_screen_cell(vt_replay_screen(joined), 0, 0)->text,
                 "a\xe2\x80\x8d" "b") == 0);
    CHECK(vt_replay_state(joined).x == 1);
    vt_replay_destroy(joined);

    /* Only a CUP separates them: not an SGR, ED 2, or another mode. */
    CHECK_REJECTS(4, 1, "\x1b[1;1Ha\xe2\x80\x8d\x1b[1mb", "no CUP");
    CHECK_REJECTS(4, 1, "\x1b[1;1Ha\xe2\x80\x8d\x1b[2Jb", "no CUP");
    CHECK_REJECTS(4, 1, "\x1b[1;1Ha\xe2\x80\x8d\x1b[?25lb", "no CUP");
    /* An extender after the sequence still has nothing to attach to. */
    CHECK_REJECTS(4, 1, "\x1b[1;1Ha\xe2\x80\x8d\x1b[1m\xe2\x80\x8d", "extender");
    return true;
}

static bool test_rows(void)
{
    char error[256];
    const TestStyleKey legend[] = { { 'r', { .fg = ovd_color_indexed(1) } } };
    const char *const too_wide[] = { "abcde" };
    CHECK(test_screen_from_rows(4, 1, classify, too_wide, nullptr, nullptr, 0, error,
                                sizeof error) == nullptr);
    CHECK(strstr(error, "row 0") != nullptr);
    const char *const wide_edge[] = { "abc世" };
    CHECK(test_screen_from_rows(4, 1, classify, wide_edge, nullptr, nullptr, 0, error,
                                sizeof error) == nullptr);
    const char *const text[] = { "a世" };
    const char *const unknown_key[] = { "x" };
    CHECK(test_screen_from_rows(4, 1, classify, text, unknown_key, legend, 1, error,
                                sizeof error) == nullptr);
    CHECK(strstr(error, "'x'") != nullptr);
    const char *const split_key[] = { ".r." };
    CHECK(test_screen_from_rows(4, 1, classify, text, split_key, legend, 1, error,
                                sizeof error) == nullptr);
    CHECK(strstr(error, "column 1") != nullptr);
    const char *const long_style[] = { "....." };
    CHECK(test_screen_from_rows(4, 1, classify, text, long_style, legend, 1, error,
                                sizeof error) == nullptr);
    const char *const control[] = { "a\tb" };
    CHECK(test_screen_from_rows(4, 1, classify, control, nullptr, nullptr, 0, error,
                                sizeof error) == nullptr);
    const char *const invalid[] = { "a\xff" };
    CHECK(test_screen_from_rows(4, 1, classify, invalid, nullptr, nullptr, 0, error,
                                sizeof error) == nullptr);
    const char *const lone[] = { "\xcc\x81" };
    CHECK(test_screen_from_rows(4, 1, classify, lone, nullptr, nullptr, 0, error,
                                sizeof error) == nullptr);
    /* A row segments like one text call: a ZWJ joins the next code point. */
    const char *const joined_row[] = { "a\xe2\x80\x8d" "b" };
    TestScreen *joined = test_screen_from_rows(4, 1, classify, joined_row, nullptr, nullptr, 0,
                                               error, sizeof error);
    CHECK(joined != nullptr);
    CHECK(test_screen_cell(joined, 0, 0)->length == 5);
    CHECK(strcmp(test_screen_cell(joined, 1, 0)->text, " ") == 0);
    test_screen_destroy(joined);

    const char *const good_styles[] = { "rrr" };
    TestScreen *screen = test_screen_from_rows(4, 1, classify, text, good_styles, legend, 1,
                                               error, sizeof error);
    CHECK(screen != nullptr);
    const TestCell *cell = test_screen_cell(screen, 1, 0);
    CHECK(cell->width == 2 && cell->style.fg.kind == OVD_COLOR_KIND_INDEXED);
    CHECK(test_screen_cell(screen, 2, 0)->style.fg.index == 1);
    CHECK(test_screen_cell(screen, 4, 0) == nullptr);
    CHECK(test_screen_cell(screen, -1, 0) == nullptr);

    /* Different sizes never compare equal. */
    TestScreen *other = test_screen_create(5, 1);
    CHECK(other != nullptr);
    char message[256];
    CHECK(!test_screen_equal(screen, other, message, sizeof message));
    CHECK(strstr(message, "size") != nullptr);
    test_screen_destroy(other);
    test_screen_destroy(screen);
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "blank_and_size", test_blank_and_size },
        { "cup_and_rgb_text", test_cup_and_rgb_text },
        { "cup_validation", test_cup_validation },
        { "sgr", test_sgr },
        { "wide_glyphs", test_wide_glyphs },
        { "pending_wrap", test_pending_wrap },
        { "erase", test_erase },
        { "modes", test_modes },
        { "unknown_sequences", test_unknown_sequences },
        { "extenders", test_extenders },
        { "zwj_final", test_zwj_final },
        { "rows", test_rows },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    puts("vt_replay: CUP, SGR, wide glyphs, ED 2, pending wrap, modes, rejections passed");
    return EXIT_SUCCESS;
}
