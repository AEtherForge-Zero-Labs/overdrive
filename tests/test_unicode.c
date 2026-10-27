/* Tests for src/unicode.c: R15 (graphemes), R16 (width), R17 (replacement),
 * against the pinned UCD. Expectations name each grapheme's cell text,
 * width, replacement flag, and the input bytes it covers. */
#include "unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FFFD "\xEF\xBF\xBD"
#define ZWJ "\xE2\x80\x8D"
#define ACUTE "\xCC\x81" /* U+0301 COMBINING ACUTE ACCENT, Mn */

/* One expected grapheme: its cell text, width, flag, and input span. */
typedef struct Expected {
    const char *text;
    uint8_t width;
    bool replaced;
    size_t consumed;
} Expected;

static int failures = 0;

static void dump(const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        fprintf(stderr, "%02X ", (unsigned)(unsigned char)bytes[i]);
    }
}

/* Segments input[0..length) and compares every grapheme with expected. */
static void check_case(int line, const char *input, size_t length,
                       const Expected *expected, size_t count)
{
    size_t offset = 0;
    size_t index = 0;
    size_t start = 0;
    OvdGrapheme grapheme;
    while (ovd_unicode_next_grapheme(input, length, &offset, &grapheme)) {
        if (index >= count) {
            fprintf(stderr, "%s:%d: extra grapheme %zu: ", __FILE__, line, index);
            dump(grapheme.bytes, grapheme.length);
            fputc('\n', stderr);
            ++failures;
            return;
        }
        const Expected *want = &expected[index];
        const size_t want_length = strlen(want->text);
        if (grapheme.length != want_length
                || memcmp(grapheme.bytes, want->text, want_length) != 0
                || grapheme.width != want->width
                || grapheme.replaced != want->replaced
                || offset - start != want->consumed) {
            fprintf(stderr, "%s:%d: grapheme %zu: got [", __FILE__, line, index);
            dump(grapheme.bytes, grapheme.length);
            fprintf(stderr, "] width %u replaced %d consumed %zu; want [",
                    (unsigned)grapheme.width, (int)grapheme.replaced, offset - start);
            dump(want->text, want_length);
            fprintf(stderr, "] width %u replaced %d consumed %zu\n",
                    (unsigned)want->width, (int)want->replaced, want->consumed);
            ++failures;
            return;
        }
        start = offset;
        ++index;
    }
    if (index != count || offset != length) {
        fprintf(stderr, "%s:%d: got %zu graphemes ending at %zu; want %zu ending at %zu\n",
                __FILE__, line, index, offset, count, length);
        ++failures;
    }
}

#define CASE(input, ...) \
    do { \
        static const Expected want_[] = { __VA_ARGS__ }; \
        check_case(__LINE__, (input), sizeof(input) - 1, want_, \
                   sizeof(want_) / sizeof(want_[0])); \
    } while (false)

/* Summed width of every grapheme: the column advance, and so the value
 * ovd_canvas_text returns for the text (R17: "the count matters"). */
static size_t total_width(const char *input, size_t length)
{
    size_t offset = 0;
    size_t width = 0;
    OvdGrapheme grapheme;
    while (ovd_unicode_next_grapheme(input, length, &offset, &grapheme)) {
        width += grapheme.width;
    }
    return width;
}

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

static void test_end_of_text(void)
{
    OvdGrapheme grapheme = { .length = 7, .width = 9 };
    size_t offset = 0;
    CHECK(!ovd_unicode_next_grapheme("", 0, &offset, &grapheme));
    CHECK(!ovd_unicode_next_grapheme(nullptr, 3, &offset, &grapheme));
    CHECK(!ovd_unicode_next_grapheme("abc", 3, nullptr, &grapheme));
    CHECK(!ovd_unicode_next_grapheme("abc", 3, &offset, nullptr));
    offset = 3;
    CHECK(!ovd_unicode_next_grapheme("abc", 3, &offset, &grapheme));
    offset = 99;
    CHECK(!ovd_unicode_next_grapheme("abc", 3, &offset, &grapheme));
    CHECK(offset == 99);
    CHECK(grapheme.length == 7 && grapheme.width == 9);
}

static void test_valid_text(void)
{
    /* ASCII: one cell per byte. */
    CASE("Hi!", { "H", 1, false, 1 }, { "i", 1, false, 1 }, { "!", 1, false, 1 });
    /* CJK (W), fullwidth (F), ambiguous (A is narrow), literal U+FFFD. */
    CASE("\xE4\xB8\xAD", { "\xE4\xB8\xAD", 2, false, 3 });
    CASE("\xEF\xBC\x81", { "\xEF\xBC\x81", 2, false, 3 });
    CASE("\xC2\xA1", { "\xC2\xA1", 1, false, 2 });
    CASE(FFFD, { FFFD, 1, false, 3 });
    /* Unassigned code points are bases with their EastAsianWidth.txt
     * width: U+FA6E and U+3FFFD (default W), U+0378 (default N). */
    CASE("\xEF\xA9\xAE", { "\xEF\xA9\xAE", 2, false, 3 });
    CASE("\xF0\xBF\xBF\xBD", { "\xF0\xBF\xBF\xBD", 2, false, 4 });
    CASE("\xCD\xB8", { "\xCD\xB8", 1, false, 2 });
    /* Highest code point, U+10FFFF. */
    CASE("\xF4\x8F\xBF\xBF", { "\xF4\x8F\xBF\xBF", 1, false, 4 });
    /* Combining marks: Mn and Me attach to the base. */
    CASE("e" ACUTE "x", { "e" ACUTE, 1, false, 3 }, { "x", 1, false, 1 });
    CASE("a\xE2\x83\x9D", { "a\xE2\x83\x9D", 1, false, 4 });
    CASE("\xE4\xB8\xAD" ACUTE, { "\xE4\xB8\xAD" ACUTE, 2, false, 5 });
    /* VS16 never changes the width: U+2764 is N, U+1F600 is W. */
    CASE("\xE2\x9D\xA4\xEF\xB8\x8F", { "\xE2\x9D\xA4\xEF\xB8\x8F", 1, false, 6 });
    CASE("\xF0\x9F\x98\x80\xEF\xB8\x8F", { "\xF0\x9F\x98\x80\xEF\xB8\x8F", 2, false, 7 });
    /* VS17 (U+E0100) is an extender too. */
    CASE("a\xF3\xA0\x84\x80", { "a\xF3\xA0\x84\x80", 1, false, 5 });
    /* ZWJ sequence: one grapheme of its base's width (man technologist). */
    CASE("\xF0\x9F\x91\xA8" ZWJ "\xF0\x9F\x92\xBB",
         { "\xF0\x9F\x91\xA8" ZWJ "\xF0\x9F\x92\xBB", 2, false, 11 });
    CASE("a" ZWJ "\xF0\x9F\x98\x80", { "a" ZWJ "\xF0\x9F\x98\x80", 1, false, 8 });
    CASE("\xF0\x9F\x98\x80" ZWJ "a", { "\xF0\x9F\x98\x80" ZWJ "a", 2, false, 8 });
    /* The joined code point takes its own extenders; ZWJ at the end. */
    CASE("a" ZWJ "b" ACUTE "c", { "a" ZWJ "b" ACUTE, 1, false, 7 }, { "c", 1, false, 1 });
    CASE("a" ZWJ, { "a" ZWJ, 1, false, 4 });
    /* Every non-bidi Cf is an extender (named deviations from UAX #29):
     * U+00AD, U+200B, U+0600 (Prepend there), U+FEFF, tag U+E0041. */
    CASE("a\xC2\xAD" "b", { "a\xC2\xAD", 1, false, 3 }, { "b", 1, false, 1 });
    CASE("a\xE2\x80\x8B", { "a\xE2\x80\x8B", 1, false, 4 });
    CASE("a\xD8\x80" "b", { "a\xD8\x80", 1, false, 3 }, { "b", 1, false, 1 });
    CASE("a\xEF\xBB\xBF", { "a\xEF\xBB\xBF", 1, false, 4 });
    CASE("a\xF3\xA0\x81\x81", { "a\xF3\xA0\x81\x81", 1, false, 5 });
    /* Not extenders: Mc (U+0903), emoji modifiers (U+1F3FB), regional
     * indicators (no pairing). */
    CASE("a\xE0\xA4\x83", { "a", 1, false, 1 }, { "\xE0\xA4\x83", 1, false, 3 });
    CASE("\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBB",
         { "\xF0\x9F\x91\x8B", 2, false, 4 }, { "\xF0\x9F\x8F\xBB", 2, false, 4 });
    CASE("\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5",
         { "\xF0\x9F\x87\xAF", 1, false, 4 }, { "\xF0\x9F\x87\xB5", 1, false, 4 });
}

static void test_ill_formed_utf8(void)
{
    /* One U+FFFD per maximal subpart (Unicode Ch. 3). */
    CASE("\xF0\x80\x80", { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 });
    CASE("\xED\xA0\x80", { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 });
    CASE("\xF0\x9F\x98", { FFFD, 1, true, 3 });
    CHECK(total_width("\xF0\x80\x80", 3) == 3);
    CHECK(total_width("\xED\xA0\x80", 3) == 3);
    CHECK(total_width("\xF0\x9F\x98", 3) == 1);
    /* Overlong, lone trail, invalid lead bytes, above U+10FFFF. */
    CASE("\xC0\x80", { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 });
    CASE("\xC1\xBF", { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 });
    CASE("\xE0\x80\x80", { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 });
    CASE("\x80" "a", { FFFD, 1, true, 1 }, { "a", 1, false, 1 });
    CASE("\xFF\xF5", { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 });
    CASE("\xF4\x90\x80\x80",
         { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 });
    /* F5..FF never lead, even before trail bytes: one U+FFFD per byte. */
    for (unsigned lead = 0xF5; lead <= 0xFF; ++lead) {
        const char text[] = { (char)lead, (char)0x80, (char)0x80, (char)0x80 };
        const Expected want[] = {
            { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 }, { FFFD, 1, true, 1 },
        };
        check_case(__LINE__, text, 4, want, 4);
    }
    /* A valid prefix cut by a non-trail byte: the byte starts anew. */
    CASE("\xE4\xB8" "a", { FFFD, 1, true, 2 }, { "a", 1, false, 1 });
    CASE("\xF0\x9F\x98\xE4\xB8\xAD", { FFFD, 1, true, 3 }, { "\xE4\xB8\xAD", 2, false, 3 });
    /* A mark truncated by length. */
    CASE("e\xCC", { "e", 1, false, 1 }, { FFFD, 1, true, 1 });
    /* Truncated by length mid-text: the bytes past length are not read. */
    {
        static const char text[] = "\xE4\xB8\xAD";
        CHECK(total_width(text, 2) == 1);
    }
}

static void test_controls(void)
{
    /* Every C0 control, including NUL, newline, tab, and ESC. */
    for (unsigned c = 0x00; c <= 0x1F; ++c) {
        const char text[] = { (char)c };
        const Expected want[] = { { FFFD, 1, true, 1 } };
        check_case(__LINE__, text, 1, want, 1);
    }
    CASE("\x7F", { FFFD, 1, true, 1 });
    /* Every C1 control, U+0080..U+009F (C2 80..C2 9F). */
    for (unsigned c = 0x80; c <= 0x9F; ++c) {
        const char text[] = { (char)0xC2, (char)c };
        const Expected want[] = { { FFFD, 1, true, 2 } };
        check_case(__LINE__, text, 2, want, 1);
    }
    /* NUL does not terminate. */
    CASE("a\0b", { "a", 1, false, 1 }, { FFFD, 1, true, 1 }, { "b", 1, false, 1 });
    /* An ESC sequence: no byte of it survives as a control. */
    CASE("\x1B[2J", { FFFD, 1, true, 1 }, { "[", 1, false, 1 },
         { "2", 1, false, 1 }, { "J", 1, false, 1 });
    /* Line and paragraph separators. */
    CASE("\xE2\x80\xA8", { FFFD, 1, true, 3 });
    CASE("\xE2\x80\xA9", { FFFD, 1, true, 3 });
    /* Bidi controls: U+061C, U+200E, U+200F, U+202A..U+202E,
     * U+2066..U+2069, each replaced even after a base. */
    static const char *const bidi[] = {
        "\xD8\x9C", "\xE2\x80\x8E", "\xE2\x80\x8F", "\xE2\x80\xAA", "\xE2\x80\xAB",
        "\xE2\x80\xAC", "\xE2\x80\xAD", "\xE2\x80\xAE", "\xE2\x81\xA6", "\xE2\x81\xA7",
        "\xE2\x81\xA8", "\xE2\x81\xA9",
    };
    for (size_t i = 0; i < sizeof(bidi) / sizeof(bidi[0]); ++i) {
        char text[8] = "a";
        const size_t length = strlen(bidi[i]);
        memcpy(text + 1, bidi[i], length);
        const Expected want[] = { { "a", 1, false, 1 }, { FFFD, 1, true, length } };
        check_case(__LINE__, text, length + 1, want, 2);
    }
    /* U+206A (deprecated format, Cf, not bidi) stays an extender. */
    CASE("a\xE2\x81\xAA", { "a\xE2\x81\xAA", 1, false, 4 });
}

static void test_replaced_bases(void)
{
    /* Lone combining mark at the start of text. */
    CASE(ACUTE, { FFFD, 1, true, 2 });
    CASE(ACUTE "a", { FFFD, 1, true, 2 }, { "a", 1, false, 1 });
    /* Two leading marks: one U+FFFD carrying the second. */
    CASE(ACUTE ACUTE, { FFFD ACUTE, 1, true, 4 });
    /* Extenders after any U+FFFD attach to it. */
    CASE("\x01" ACUTE, { FFFD ACUTE, 1, true, 3 });
    CASE("\xFF" ACUTE "a", { FFFD ACUTE, 1, true, 3 }, { "a", 1, false, 1 });
    CASE("\xE2\x80\xAE" ACUTE, { FFFD ACUTE, 1, true, 5 });
    /* A lone variation selector. */
    CASE("\xEF\xB8\x8F", { FFFD, 1, true, 3 });
    /* Leading ZWJ: replaced, joins nothing. */
    CASE(ZWJ "a", { FFFD, 1, true, 3 }, { "a", 1, false, 1 });
    CASE(ZWJ "\xF0\x9F\x98\x80", { FFFD, 1, true, 3 }, { "\xF0\x9F\x98\x80", 2, false, 4 });
    /* A ZWJ after a replaced base is a real ZWJ and joins. */
    CASE("\x01" ZWJ "a", { FFFD ZWJ "a", 1, true, 5 });
    /* After a ZWJ, a code point R17 replaces starts a new grapheme. */
    CASE("\xF0\x9F\x98\x80" ZWJ "\x01",
         { "\xF0\x9F\x98\x80" ZWJ, 2, false, 7 }, { FFFD, 1, true, 1 });
    CASE("a" ZWJ "\xE2\x80\x8F", { "a" ZWJ, 1, false, 4 }, { FFFD, 1, true, 3 });
    CASE("a" ZWJ "\xFF", { "a" ZWJ, 1, false, 4 }, { FFFD, 1, true, 1 });
    CASE("a" ZWJ "\xE2\x80\xA8", { "a" ZWJ, 1, false, 4 }, { FFFD, 1, true, 3 });
}

static void test_length_limit(void)
{
    /* Exactly 15 bytes fits: 'a' + 7 marks. */
    CASE("a" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE,
         { "a" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE, 1, false, 15 });
    /* 17 bytes: the whole grapheme becomes one U+FFFD, width 1. */
    CASE("a" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE "b",
         { FFFD, 1, true, 17 }, { "b", 1, false, 1 });
    /* A wide base: 15 bytes keep width 2; over the limit, width 1. */
    CASE("\xE4\xB8\xAD" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE,
         { "\xE4\xB8\xAD" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE, 2, false, 15 });
    CASE("\xE4\xB8\xAD" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE,
         { FFFD, 1, true, 17 });
    /* The limit applies to a replaced base's cell text: U+FFFD + 6 marks
     * is 15 bytes; + 7 marks is 17, so one bare U+FFFD. */
    CASE(ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE,
         { FFFD ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE, 1, true, 14 });
    CASE(ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE, { FFFD, 1, true, 16 });
    /* Exactly 16 bytes, one past the limit: 'a' + 6 marks + U+200B. */
    CASE("a" ACUTE ACUTE ACUTE ACUTE ACUTE ACUTE "\xE2\x80\x8B" "b",
         { FFFD, 1, true, 16 }, { "b", 1, false, 1 });
    /* A replaced base whose cell text reaches 16 bytes: U+FFFD (for FF)
     * + 5 marks + U+200B, from 14 input bytes. */
    CASE("\xFF" ACUTE ACUTE ACUTE ACUTE ACUTE "\xE2\x80\x8B" "b",
         { FFFD, 1, true, 14 }, { "b", 1, false, 1 });
    /* Four-person family: 25 bytes of ZWJ sequence, one U+FFFD. */
    CASE("\xF0\x9F\x91\xA8" ZWJ "\xF0\x9F\x91\xA9" ZWJ "\xF0\x9F\x91\xA7" ZWJ
         "\xF0\x9F\x91\xA6" "a",
         { FFFD, 1, true, 25 }, { "a", 1, false, 1 });
    CHECK(total_width("\xF0\x9F\x91\xA8" ZWJ "\xF0\x9F\x91\xA9" ZWJ
                      "\xF0\x9F\x91\xA7" ZWJ "\xF0\x9F\x91\xA6", 25) == 1);
}

int main(void)
{
    test_end_of_text();
    test_valid_text();
    test_ill_formed_utf8();
    test_controls();
    test_replaced_bases();
    test_length_limit();
    if (failures != 0) {
        fprintf(stderr, "unicode: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("unicode: decoding, segmentation, width, and replacement passed");
    return EXIT_SUCCESS;
}
