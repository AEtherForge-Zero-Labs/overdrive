#include "color.h"

#include <stdint.h>
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

/* ---- Readable SGR (Art. VI) ------------------------------------------ */

/* Renders bytes with ESC shown as "ESC" so expectations read as text. */
static void readable(const char *bytes, size_t length, char *out, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < length && used + 4 < capacity; ++i) {
        if (bytes[i] == '\x1b') {
            memcpy(out + used, "ESC", 3);
            used += 3;
        } else {
            out[used++] = bytes[i];
        }
    }
    out[used] = '\0';
}

/* Encodes from → to at depth and compares the readable result. */
static bool sgr_is(OvdStyle from, OvdStyle to, OvdColorDepth depth, const char *expected,
                   int line)
{
    char bytes[OVD_COLOR_SGR_MAX_LENGTH];
    size_t length = 999;
    if (!ovd_color_sgr_encode(from, to, depth, bytes, sizeof bytes, &length)) {
        fprintf(stderr, "%s:%d: encode refused\n", __FILE__, line);
        return false;
    }
    char text[4 * OVD_COLOR_SGR_MAX_LENGTH + 1];
    readable(bytes, length, text, sizeof text);
    if (strcmp(text, expected) != 0) {
        fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", __FILE__, line, expected,
                text);
        return false;
    }
    return true;
}

#define SGR_IS(from, to, depth, expected) \
    CHECK(sgr_is((from), (to), (depth), (expected), __LINE__))

static OvdStyle style(OvdColor fg, OvdColor bg, uint16_t attrs)
{
    return (OvdStyle){ .fg = fg, .bg = bg, .attrs = attrs };
}

static OvdStyle attrs_only(uint16_t attrs)
{
    return style(OVD_COLOR_DEFAULT, OVD_COLOR_DEFAULT, attrs);
}

static bool color_equal(OvdColor a, OvdColor b)
{
    return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

static const OvdColorDepth depths[] = {
    OVD_COLOR_DEPTH_NONE,
    OVD_COLOR_DEPTH_16,
    OVD_COLOR_DEPTH_256,
    OVD_COLOR_DEPTH_TRUECOLOR,
};

/* ---- Reference palettes (xterm defaults; research R9, R37) ------------- */

static const uint8_t xterm16[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0xcd, 0x00, 0x00 }, { 0x00, 0xcd, 0x00 }, { 0xcd, 0xcd, 0x00 },
    { 0x00, 0x00, 0xee }, { 0xcd, 0x00, 0xcd }, { 0x00, 0xcd, 0xcd }, { 0xe5, 0xe5, 0xe5 },
    { 0x7f, 0x7f, 0x7f }, { 0xff, 0x00, 0x00 }, { 0x00, 0xff, 0x00 }, { 0xff, 0xff, 0x00 },
    { 0x5c, 0x5c, 0xff }, { 0xff, 0x00, 0xff }, { 0x00, 0xff, 0xff }, { 0xff, 0xff, 0xff },
};

static void xterm256_rgb(int index, int rgb[3])
{
    static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
    if (index >= 232) {
        rgb[0] = rgb[1] = rgb[2] = 8 + 10 * (index - 232);
    } else {
        const int cube = index - 16;
        rgb[0] = levels[cube / 36];
        rgb[1] = levels[(cube / 6) % 6];
        rgb[2] = levels[cube % 6];
    }
}

static int distance(int r, int g, int b, int r2, int g2, int b2)
{
    return (r - r2) * (r - r2) + (g - g2) * (g - g2) + (b - b2) * (b - b2);
}

/* Brute force: nearest of 16-255, lowest index on ties. */
static int brute256(int r, int g, int b)
{
    int best = -1;
    int best_distance = INT32_MAX;
    for (int index = 16; index < 256; ++index) {
        int rgb[3];
        xterm256_rgb(index, rgb);
        const int d = distance(r, g, b, rgb[0], rgb[1], rgb[2]);
        if (d < best_distance) {
            best = index;
            best_distance = d;
        }
    }
    return best;
}

/* Brute force: nearest xterm default 16 color, lowest index on ties. */
static int brute16(int r, int g, int b)
{
    int best = -1;
    int best_distance = INT32_MAX;
    for (int index = 0; index < 16; ++index) {
        const int d = distance(r, g, b, xterm16[index][0], xterm16[index][1], xterm16[index][2]);
        if (d < best_distance) {
            best = index;
            best_distance = d;
        }
    }
    return best;
}

/* ---- Downsampling ---------------------------------------------------- */

static bool indexed_at(OvdColor color, OvdColorDepth depth, int expected)
{
    const OvdColor got = ovd_color_downsample(color, depth);
    return color_equal(got, ovd_color_indexed((uint8_t)expected));
}

static bool test_rgb_to_256(void)
{
    const OvdColorDepth d = OVD_COLOR_DEPTH_256;
    CHECK(indexed_at(ovd_color_rgb(0, 0, 0), d, 16));
    CHECK(indexed_at(ovd_color_rgb(255, 255, 255), d, 231));
    CHECK(indexed_at(ovd_color_rgb(255, 0, 0), d, 196));
    CHECK(indexed_at(ovd_color_rgb(95, 135, 175), d, 67));
    /* Gray ramp beats the cube when closer. */
    CHECK(indexed_at(ovd_color_rgb(128, 128, 128), d, 244));
    CHECK(indexed_at(ovd_color_rgb(8, 8, 8), d, 232));
    CHECK(indexed_at(ovd_color_rgb(238, 238, 238), d, 255));
    CHECK(indexed_at(ovd_color_rgb(115, 115, 115), d, 243));
    /* A cube tie (115 is 20 from both 95 and 135) goes to the lower index. */
    CHECK(indexed_at(ovd_color_rgb(0, 0, 115), d, 17));
    CHECK(indexed_at(ovd_color_hex(0x5f87af), d, 67));
    /* Exhaustive against brute force on a grid that includes every
     * midpoint parity. */
    for (int r = 0; r < 256; r += 5) {
        for (int g = 0; g < 256; g += 3) {
            for (int b = 0; b < 256; b += 7) {
                const OvdColor rgb = ovd_color_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
                CHECK(indexed_at(rgb, d, brute256(r, g, b)));
            }
        }
    }
    for (int v = 0; v < 256; ++v) {
        CHECK(indexed_at(ovd_color_rgb((uint8_t)v, (uint8_t)v, (uint8_t)v), d,
                         brute256(v, v, v)));
        CHECK(indexed_at(ovd_color_rgb((uint8_t)v, 0, 255), d, brute256(v, 0, 255)));
    }
    return true;
}

static bool test_rgb_to_16(void)
{
    const OvdColorDepth d = OVD_COLOR_DEPTH_16;
    CHECK(indexed_at(ovd_color_rgb(0, 0, 0), d, 0));
    CHECK(indexed_at(ovd_color_rgb(205, 0, 0), d, 1));
    CHECK(indexed_at(ovd_color_rgb(255, 0, 0), d, 9));
    CHECK(indexed_at(ovd_color_rgb(229, 229, 229), d, 7));
    CHECK(indexed_at(ovd_color_rgb(127, 127, 127), d, 8));
    CHECK(indexed_at(ovd_color_rgb(0, 0, 238), d, 4));
    CHECK(indexed_at(ovd_color_rgb(92, 92, 255), d, 12));
    CHECK(indexed_at(ovd_color_rgb(255, 255, 255), d, 15));
    for (int r = 0; r < 256; r += 5) {
        for (int g = 0; g < 256; g += 3) {
            for (int b = 0; b < 256; b += 7) {
                const OvdColor rgb = ovd_color_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
                CHECK(indexed_at(rgb, d, brute16(r, g, b)));
            }
        }
    }
    return true;
}

static bool test_256_to_16(void)
{
    const OvdColorDepth d = OVD_COLOR_DEPTH_16;
    CHECK(indexed_at(ovd_color_indexed(16), d, 0));
    CHECK(indexed_at(ovd_color_indexed(196), d, 9));
    CHECK(indexed_at(ovd_color_indexed(231), d, 15));
    CHECK(indexed_at(ovd_color_indexed(244), d, 8));
    CHECK(indexed_at(ovd_color_indexed(232), d, 0));
    for (int index = 16; index < 256; ++index) {
        int rgb[3];
        xterm256_rgb(index, rgb);
        CHECK(indexed_at(ovd_color_indexed((uint8_t)index), d, brute16(rgb[0], rgb[1], rgb[2])));
    }
    return true;
}

static bool test_passthrough(void)
{
    /* Indexed 0-15 follow the user's theme at every color depth. */
    for (int index = 0; index < 16; ++index) {
        for (size_t i = 1; i < sizeof depths / sizeof depths[0]; ++i) {
            CHECK(indexed_at(ovd_color_indexed((uint8_t)index), depths[i], index));
        }
    }
    for (int index = 16; index < 256; ++index) {
        CHECK(indexed_at(ovd_color_indexed((uint8_t)index), OVD_COLOR_DEPTH_256, index));
        CHECK(indexed_at(ovd_color_indexed((uint8_t)index), OVD_COLOR_DEPTH_TRUECOLOR, index));
    }
    const OvdColor rgb = ovd_color_rgb(1, 2, 3);
    CHECK(color_equal(ovd_color_downsample(rgb, OVD_COLOR_DEPTH_TRUECOLOR), rgb));
    for (size_t i = 0; i < sizeof depths / sizeof depths[0]; ++i) {
        CHECK(color_equal(ovd_color_downsample(OVD_COLOR_DEFAULT, depths[i]), OVD_COLOR_DEFAULT));
    }
    return true;
}

static bool test_none_and_normalization(void)
{
    const OvdColor colors[] = {
        ovd_color_rgb(255, 0, 0), ovd_color_indexed(3), ovd_color_indexed(200), OVD_COLOR_DEFAULT,
    };
    for (size_t i = 0; i < sizeof colors / sizeof colors[0]; ++i) {
        CHECK(color_equal(ovd_color_downsample(colors[i], OVD_COLOR_DEPTH_NONE), OVD_COLOR_DEFAULT));
        /* Deliberately cross the closed enum boundary: an out-of-range depth
         * is treated as NONE. */
        CHECK(color_equal(ovd_color_downsample(colors[i], (OvdColorDepth)200), OVD_COLOR_DEFAULT));
    }
    /* Only the kind's fields survive. */
    const OvdColor messy_indexed = { .kind = OVD_COLOR_KIND_INDEXED, .index = 5, .r = 1, .g = 2, .b = 3 };
    CHECK(color_equal(ovd_color_downsample(messy_indexed, OVD_COLOR_DEPTH_256), ovd_color_indexed(5)));
    const OvdColor messy_rgb = { .kind = OVD_COLOR_KIND_RGB, .index = 9, .r = 1, .g = 2, .b = 3 };
    CHECK(color_equal(ovd_color_downsample(messy_rgb, OVD_COLOR_DEPTH_TRUECOLOR),
                      ovd_color_rgb(1, 2, 3)));
    const OvdColor messy_default = { .kind = OVD_COLOR_KIND_DEFAULT, .index = 9, .r = 1 };
    CHECK(color_equal(ovd_color_downsample(messy_default, OVD_COLOR_DEPTH_TRUECOLOR),
                      OVD_COLOR_DEFAULT));
    /* Deliberately cross the closed enum boundary: an unknown kind is
     * the default color (R22). */
    const OvdColor unknown = { .kind = (OvdColorKind)77, .index = 9, .r = 1 };
    CHECK(color_equal(ovd_color_downsample(unknown, OVD_COLOR_DEPTH_TRUECOLOR), OVD_COLOR_DEFAULT));
    return true;
}

/* ---- SGR encoding ---------------------------------------------------- */

static bool test_sgr_attributes(void)
{
    const OvdStyle none = OVD_STYLE_DEFAULT;
    const OvdColorDepth t = OVD_COLOR_DEPTH_TRUECOLOR;
    SGR_IS(none, none, t, "");
    SGR_IS(none, attrs_only(OVD_ATTR_BOLD), t, "ESC[1m");
    SGR_IS(none, attrs_only(OVD_ATTR_DIM), t, "ESC[2m");
    SGR_IS(none, attrs_only(OVD_ATTR_ITALIC), t, "ESC[3m");
    SGR_IS(none, attrs_only(OVD_ATTR_UNDERLINE), t, "ESC[4m");
    SGR_IS(none, attrs_only(OVD_ATTR_REVERSE), t, "ESC[7m");
    SGR_IS(none, attrs_only(OVD_ATTR_STRIKETHROUGH), t, "ESC[9m");
    SGR_IS(none, attrs_only(0x3f), t, "ESC[1;2;3;4;7;9m");
    /* Undefined attribute bits are dropped before comparing. */
    SGR_IS(none, attrs_only(0xffc0), t, "");
    SGR_IS(attrs_only(0x8000 | OVD_ATTR_BOLD), attrs_only(OVD_ATTR_BOLD), t, "");
    SGR_IS(none, attrs_only(0x8000 | OVD_ATTR_ITALIC), t, "ESC[3m");
    /* Attributes apply at every depth, NONE included. */
    for (size_t i = 0; i < sizeof depths / sizeof depths[0]; ++i) {
        SGR_IS(none, attrs_only(OVD_ATTR_UNDERLINE), depths[i], "ESC[4m");
    }
    return true;
}

static bool test_sgr_colors(void)
{
    const OvdColor def = OVD_COLOR_DEFAULT;
    const OvdStyle none = OVD_STYLE_DEFAULT;
    const OvdColorDepth d16 = OVD_COLOR_DEPTH_16;
    const OvdColorDepth d256 = OVD_COLOR_DEPTH_256;
    const OvdColorDepth tc = OVD_COLOR_DEPTH_TRUECOLOR;

    /* Indexed 0-15 use the 16-color codes at every color depth. */
    for (size_t i = 1; i < sizeof depths / sizeof depths[0]; ++i) {
        SGR_IS(none, style(ovd_color_indexed(0), def, 0), depths[i], "ESC[30m");
        SGR_IS(none, style(ovd_color_indexed(7), def, 0), depths[i], "ESC[37m");
        SGR_IS(none, style(ovd_color_indexed(8), def, 0), depths[i], "ESC[90m");
        SGR_IS(none, style(ovd_color_indexed(15), def, 0), depths[i], "ESC[97m");
        SGR_IS(none, style(def, ovd_color_indexed(0), 0), depths[i], "ESC[40m");
        SGR_IS(none, style(def, ovd_color_indexed(7), 0), depths[i], "ESC[47m");
        SGR_IS(none, style(def, ovd_color_indexed(8), 0), depths[i], "ESC[100m");
        SGR_IS(none, style(def, ovd_color_indexed(15), 0), depths[i], "ESC[107m");
    }
    /* Indexed 16-255. */
    SGR_IS(none, style(ovd_color_indexed(200), def, 0), d256, "ESC[38;5;200m");
    SGR_IS(none, style(def, ovd_color_indexed(16), 0), d256, "ESC[48;5;16m");
    SGR_IS(none, style(ovd_color_indexed(255), def, 0), tc, "ESC[38;5;255m");
    SGR_IS(none, style(ovd_color_indexed(196), def, 0), d16, "ESC[91m");
    /* RGB at each depth. */
    SGR_IS(none, style(ovd_color_rgb(1, 2, 3), def, 0), tc, "ESC[38;2;1;2;3m");
    SGR_IS(none, style(def, ovd_color_rgb(255, 128, 0), 0), tc, "ESC[48;2;255;128;0m");
    SGR_IS(none, style(ovd_color_rgb(255, 0, 0), def, 0), d256, "ESC[38;5;196m");
    SGR_IS(none, style(def, ovd_color_rgb(128, 128, 128), 0), d256, "ESC[48;5;244m");
    SGR_IS(none, style(ovd_color_rgb(255, 0, 0), def, 0), d16, "ESC[91m");
    SGR_IS(none, style(def, ovd_color_rgb(0, 0, 238), 0), d16, "ESC[44m");
    /* Both colors in one sequence: foreground first. */
    SGR_IS(none, style(ovd_color_indexed(1), ovd_color_indexed(4), 0), d16, "ESC[31;44m");
    /* Return to the default colors. */
    SGR_IS(style(ovd_color_indexed(1), def, 0), none, tc, "ESC[39m");
    SGR_IS(style(def, ovd_color_indexed(1), 0), none, tc, "ESC[49m");
    SGR_IS(style(ovd_color_rgb(1, 2, 3), ovd_color_rgb(4, 5, 6), 0), none, tc, "ESC[39;49m");
    /* Depth NONE drops colors; only attributes remain. */
    const OvdColorDepth n = OVD_COLOR_DEPTH_NONE;
    SGR_IS(none, style(ovd_color_rgb(255, 0, 0), ovd_color_indexed(3), 0), n, "");
    SGR_IS(style(ovd_color_indexed(1), def, 0), style(ovd_color_indexed(4), def, OVD_ATTR_BOLD), n,
           "ESC[1m");
    SGR_IS(style(ovd_color_indexed(1), def, OVD_ATTR_BOLD), none, n, "ESC[0m");
    /* A change the depth cannot show writes nothing. */
    SGR_IS(style(ovd_color_rgb(255, 0, 0), def, 0), style(ovd_color_rgb(254, 1, 0), def, 0), d256, "");
    SGR_IS(style(ovd_color_indexed(196), def, 0), style(ovd_color_rgb(255, 0, 0), def, 0), d16, "");
    /* An unknown kind is the default color. */
    const OvdColor unknown = { .kind = (OvdColorKind)9, .r = 255 };
    SGR_IS(none, style(unknown, unknown, 0), tc, "");
    SGR_IS(style(ovd_color_indexed(2), def, 0), style(unknown, def, 0), tc, "ESC[39m");
    return true;
}

static bool test_sgr_delta_vs_reset(void)
{
    const OvdColor def = OVD_COLOR_DEFAULT;
    const OvdColor red = ovd_color_indexed(1);
    const OvdColor blue = ovd_color_indexed(4);
    const OvdColor green = ovd_color_indexed(2);
    const OvdColorDepth tc = OVD_COLOR_DEPTH_TRUECOLOR;

    /* Attributes only added: delta with only what changed. */
    SGR_IS(style(red, def, OVD_ATTR_BOLD), style(red, def, OVD_ATTR_BOLD | OVD_ATTR_ITALIC), tc,
           "ESC[3m");
    SGR_IS(attrs_only(OVD_ATTR_BOLD), style(green, def, OVD_ATTR_BOLD | OVD_ATTR_UNDERLINE), tc,
           "ESC[4;32m");
    SGR_IS(style(red, blue, OVD_ATTR_DIM), style(red, green, OVD_ATTR_DIM), tc, "ESC[42m");
    SGR_IS(style(red, blue, OVD_ATTR_DIM), style(def, def, OVD_ATTR_DIM), tc, "ESC[39;49m");
    /* An attribute removed: reset, then the full style. */
    SGR_IS(attrs_only(OVD_ATTR_BOLD | OVD_ATTR_ITALIC), attrs_only(OVD_ATTR_BOLD), tc, "ESC[0;1m");
    SGR_IS(attrs_only(OVD_ATTR_BOLD), OVD_STYLE_DEFAULT, tc, "ESC[0m");
    SGR_IS(style(red, blue, OVD_ATTR_BOLD), style(red, def, 0), tc, "ESC[0;31m");
    SGR_IS(style(red, blue, OVD_ATTR_REVERSE), style(red, blue, OVD_ATTR_STRIKETHROUGH), tc,
           "ESC[0;9;31;44m");
    SGR_IS(style(def, def, OVD_ATTR_UNDERLINE),
           style(ovd_color_rgb(10, 20, 30), ovd_color_indexed(100), OVD_ATTR_BOLD), tc,
           "ESC[0;1;38;2;10;20;30;48;5;100m");
    return true;
}

static bool test_sgr_worst_case_and_bounds(void)
{
    const OvdColor white = ovd_color_rgb(255, 255, 255);
    const OvdColorDepth tc = OVD_COLOR_DEPTH_TRUECOLOR;
    SGR_IS(OVD_STYLE_DEFAULT, style(white, white, 0x3f), tc,
           "ESC[1;2;3;4;7;9;38;2;255;255;255;48;2;255;255;255m");
    SGR_IS(attrs_only(0x3f), style(white, white, 0x3f & ~OVD_ATTR_STRIKETHROUGH), tc,
           "ESC[0;1;2;3;4;7;38;2;255;255;255;48;2;255;255;255m");

    const OvdStyle from = OVD_STYLE_DEFAULT;
    const OvdStyle to = style(white, white, 0x3f);
    char buffer[OVD_COLOR_SGR_MAX_LENGTH + 8];
    size_t length = 0;
    CHECK(ovd_color_sgr_encode(from, to, tc, buffer, sizeof buffer, &length));
    CHECK(length == OVD_COLOR_SGR_MAX_LENGTH);

    /* One byte short: refused, nothing written. */
    memset(buffer, 'X', sizeof buffer);
    length = 12345;
    CHECK(!ovd_color_sgr_encode(from, to, tc, buffer, OVD_COLOR_SGR_MAX_LENGTH - 1, &length));
    CHECK(length == 12345);
    for (size_t i = 0; i < sizeof buffer; ++i) {
        CHECK(buffer[i] == 'X');
    }
    /* Exact fit, and nothing past it is touched. */
    CHECK(ovd_color_sgr_encode(from, to, tc, buffer, OVD_COLOR_SGR_MAX_LENGTH, &length));
    CHECK(length == OVD_COLOR_SGR_MAX_LENGTH);
    CHECK(buffer[OVD_COLOR_SGR_MAX_LENGTH] == 'X');
    /* Zero capacity and nullptr buffers. */
    length = 7;
    CHECK(!ovd_color_sgr_encode(from, to, tc, buffer, 0, &length));
    CHECK(!ovd_color_sgr_encode(from, to, tc, nullptr, 100, &length));
    CHECK(length == 7);
    CHECK(!ovd_color_sgr_encode(from, to, tc, buffer, sizeof buffer, nullptr));
    /* Nothing to write needs no buffer. */
    CHECK(ovd_color_sgr_encode(from, from, tc, nullptr, 0, &length));
    CHECK(length == 0);
    return true;
}

/* ---- Semantic check: replaying the output reaches the target --------- */

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static OvdColor random_color(uint32_t *state)
{
    const uint32_t pick = next_random(state);
    switch (pick % 4) {
    case 0:
        return OVD_COLOR_DEFAULT;
    case 1:
        return ovd_color_indexed((uint8_t)(next_random(state) % 16));
    case 2:
        return ovd_color_indexed((uint8_t)next_random(state));
    default:
        return ovd_color_hex(next_random(state));
    }
}

/* Applies one SGR sequence (R7 subset only) to a style. */
static bool apply_sgr(OvdStyle *style_state, const char *bytes, size_t length)
{
    if (length == 0) {
        return true;
    }
    if (length < 3 || bytes[0] != '\x1b' || bytes[1] != '[' || bytes[length - 1] != 'm') {
        return false;
    }
    int params[64];
    size_t count = 0;
    int value = 0;
    bool digits = false;
    for (size_t i = 2; i < length; ++i) {
        const char c = bytes[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digits = true;
            if (value > 255) {
                return false;
            }
        } else if (c == ';' || c == 'm') {
            if (!digits || count == 64) {
                return false;
            }
            params[count++] = value;
            value = 0;
            digits = false;
        } else {
            return false;
        }
    }
    static const struct { int code; uint16_t attr; } attr_codes[] = {
        { 1, OVD_ATTR_BOLD }, { 2, OVD_ATTR_DIM }, { 3, OVD_ATTR_ITALIC },
        { 4, OVD_ATTR_UNDERLINE }, { 7, OVD_ATTR_REVERSE }, { 9, OVD_ATTR_STRIKETHROUGH },
    };
    for (size_t i = 0; i < count; ++i) {
        const int p = params[i];
        bool known = false;
        if (p == 0) {
            *style_state = OVD_STYLE_DEFAULT;
            known = true;
        }
        for (size_t a = 0; a < sizeof attr_codes / sizeof attr_codes[0]; ++a) {
            if (p == attr_codes[a].code) {
                style_state->attrs = (uint16_t)(style_state->attrs | attr_codes[a].attr);
                known = true;
            }
        }
        OvdColor *target = nullptr;
        if ((p >= 30 && p <= 39) || (p >= 90 && p <= 97)) {
            target = &style_state->fg;
        } else if ((p >= 40 && p <= 49) || (p >= 100 && p <= 107)) {
            target = &style_state->bg;
        }
        if (target != nullptr) {
            known = true;
            const int base = p % 10;
            if (p == 39 || p == 49) {
                *target = OVD_COLOR_DEFAULT;
            } else if (p == 38 || p == 48) {
                if (i + 2 < count && params[i + 1] == 5) {
                    *target = ovd_color_indexed((uint8_t)params[i + 2]);
                    i += 2;
                } else if (i + 4 < count && params[i + 1] == 2) {
                    *target = ovd_color_rgb((uint8_t)params[i + 2], (uint8_t)params[i + 3],
                                            (uint8_t)params[i + 4]);
                    i += 4;
                } else {
                    return false;
                }
            } else if (p >= 90) {
                *target = ovd_color_indexed((uint8_t)(8 + base));
            } else {
                *target = ovd_color_indexed((uint8_t)base);
            }
        }
        if (!known) {
            return false;
        }
    }
    return true;
}

static OvdStyle normalized(OvdStyle s, OvdColorDepth depth)
{
    return (OvdStyle){
        .fg = ovd_color_downsample(s.fg, depth),
        .bg = ovd_color_downsample(s.bg, depth),
        .attrs = (uint16_t)(s.attrs & 0x3f),
    };
}

static bool style_equal(OvdStyle a, OvdStyle b)
{
    return color_equal(a.fg, b.fg) && color_equal(a.bg, b.bg) && a.attrs == b.attrs;
}

static bool test_sgr_replay(void)
{
    uint32_t state = 20261026u;
    size_t longest = 0;
    for (int round = 0; round < 200000; ++round) {
        const OvdStyle from = style(random_color(&state), random_color(&state),
                                    (uint16_t)next_random(&state));
        const OvdStyle to = style(random_color(&state), random_color(&state),
                                  (uint16_t)next_random(&state));
        const OvdColorDepth depth = depths[next_random(&state) % 4];
        char bytes[OVD_COLOR_SGR_MAX_LENGTH];
        size_t length = 0;
        CHECK(ovd_color_sgr_encode(from, to, depth, bytes, sizeof bytes, &length));
        CHECK(length <= OVD_COLOR_SGR_MAX_LENGTH);
        longest = length > longest ? length : longest;

        OvdStyle replayed = normalized(from, depth);
        CHECK(apply_sgr(&replayed, bytes, length));
        CHECK(style_equal(replayed, normalized(to, depth)));
        /* Nothing is written exactly when nothing changes. */
        CHECK((length == 0) == style_equal(normalized(from, depth), normalized(to, depth)));
        /* A delta never resets; a removal always does. */
        const uint16_t removed = (uint16_t)(from.attrs & 0x3f & ~to.attrs);
        CHECK((length >= 4 && bytes[2] == '0' && (bytes[3] == ';' || bytes[3] == 'm'))
              == (removed != 0));
    }
    CHECK(longest > 30);
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "rgb_to_256", test_rgb_to_256 },
        { "rgb_to_16", test_rgb_to_16 },
        { "256_to_16", test_256_to_16 },
        { "passthrough", test_passthrough },
        { "none_and_normalization", test_none_and_normalization },
        { "sgr_attributes", test_sgr_attributes },
        { "sgr_colors", test_sgr_colors },
        { "sgr_delta_vs_reset", test_sgr_delta_vs_reset },
        { "sgr_worst_case_and_bounds", test_sgr_worst_case_and_bounds },
        { "sgr_replay", test_sgr_replay },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (tests[i].run()) {
            printf("ok   %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    puts("color: downsampling, SGR encodings, delta/reset, bounds, and replay passed");
    return EXIT_SUCCESS;
}
