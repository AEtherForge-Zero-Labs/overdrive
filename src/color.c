#include "color.h"

#include <stdint.h>
#include <string.h>

/* Attribute bits defined by OvdAttr; anything else is dropped. */
#define OVD_COLOR_ATTR_MASK \
    ((uint16_t)(OVD_ATTR_BOLD | OVD_ATTR_DIM | OVD_ATTR_ITALIC | OVD_ATTR_UNDERLINE \
                | OVD_ATTR_REVERSE | OVD_ATTR_STRIKETHROUGH))

/* xterm's default values for palette entries 0-15 (xterm's XTerm-col.ad
 * resources color0-color15; research R37). */
static const uint8_t xterm_16[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0xcd, 0x00, 0x00 }, { 0x00, 0xcd, 0x00 }, { 0xcd, 0xcd, 0x00 },
    { 0x00, 0x00, 0xee }, { 0xcd, 0x00, 0xcd }, { 0x00, 0xcd, 0xcd }, { 0xe5, 0xe5, 0xe5 },
    { 0x7f, 0x7f, 0x7f }, { 0xff, 0x00, 0x00 }, { 0x00, 0xff, 0x00 }, { 0xff, 0xff, 0x00 },
    { 0x5c, 0x5c, 0xff }, { 0xff, 0x00, 0xff }, { 0x00, 0xff, 0xff }, { 0xff, 0xff, 0xff },
};

/* The six levels of each axis of xterm's 6x6x6 cube (indices 16-231). */
static const uint8_t cube_levels[6] = { 0, 95, 135, 175, 215, 255 };

static int32_t squared_distance(int32_t r, int32_t g, int32_t b, int32_t r2, int32_t g2,
                                int32_t b2)
{
    return (r - r2) * (r - r2) + (g - g2) * (g - g2) + (b - b2) * (b - b2);
}

/* xterm's RGB value of palette entry 16-255. */
static void palette_rgb(uint8_t index, uint8_t rgb[3])
{
    if (index >= 232) {
        const uint8_t gray = (uint8_t)(8 + 10 * (index - 232));
        rgb[0] = rgb[1] = rgb[2] = gray;
        return;
    }
    const int cube = index - 16;
    rgb[0] = cube_levels[cube / 36];
    rgb[1] = cube_levels[(cube / 6) % 6];
    rgb[2] = cube_levels[cube % 6];
}

/* Nearest cube level to value; ties go to the lower level (lower index). */
static int nearest_level(uint8_t value)
{
    int best = 0;
    int best_distance = INT32_MAX;
    for (int level = 0; level < 6; ++level) {
        const int d = (value - cube_levels[level]) * (value - cube_levels[level]);
        if (d < best_distance) {
            best = level;
            best_distance = d;
        }
    }
    return best;
}

/* Nearest of indices 16-255, lowest index on ties. Squared distance is
 * separable, so the per-axis nearest levels give the nearest cube entry,
 * and per-axis ties resolved low give its lowest index. Every cube index
 * is below every gray index, so the cube wins a tie with the ramp. */
static uint8_t nearest_256(uint8_t r, uint8_t g, uint8_t b)
{
    const int lr = nearest_level(r);
    const int lg = nearest_level(g);
    const int lb = nearest_level(b);
    const uint8_t cube_index = (uint8_t)(16 + 36 * lr + 6 * lg + lb);
    const int32_t cube_distance =
        squared_distance(r, g, b, cube_levels[lr], cube_levels[lg], cube_levels[lb]);

    uint8_t best = cube_index;
    int32_t best_distance = cube_distance;
    for (int step = 0; step < 24; ++step) {
        const int32_t gray = 8 + 10 * step;
        const int32_t d = squared_distance(r, g, b, gray, gray, gray);
        if (d < best_distance) {
            best = (uint8_t)(232 + step);
            best_distance = d;
        }
    }
    return best;
}

/* Nearest xterm default 16 color, lowest index on ties. */
static uint8_t nearest_16(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t best = 0;
    int32_t best_distance = INT32_MAX;
    for (uint8_t index = 0; index < 16; ++index) {
        const int32_t d =
            squared_distance(r, g, b, xterm_16[index][0], xterm_16[index][1], xterm_16[index][2]);
        if (d < best_distance) {
            best = index;
            best_distance = d;
        }
    }
    return best;
}

OvdColor ovd_color_downsample(OvdColor color, OvdColorDepth depth)
{
    switch (depth) {
    case OVD_COLOR_DEPTH_16:
    case OVD_COLOR_DEPTH_256:
    case OVD_COLOR_DEPTH_TRUECOLOR:
        break;
    case OVD_COLOR_DEPTH_NONE:
    default:
        return OVD_COLOR_DEFAULT;
    }

    switch (color.kind) {
    case OVD_COLOR_KIND_INDEXED:
        if (color.index < 16 || depth != OVD_COLOR_DEPTH_16) {
            return ovd_color_indexed(color.index);
        }
        uint8_t rgb[3];
        palette_rgb(color.index, rgb);
        return ovd_color_indexed(nearest_16(rgb[0], rgb[1], rgb[2]));
    case OVD_COLOR_KIND_RGB:
        if (depth == OVD_COLOR_DEPTH_TRUECOLOR) {
            return ovd_color_rgb(color.r, color.g, color.b);
        }
        if (depth == OVD_COLOR_DEPTH_256) {
            return ovd_color_indexed(nearest_256(color.r, color.g, color.b));
        }
        return ovd_color_indexed(nearest_16(color.r, color.g, color.b));
    case OVD_COLOR_KIND_DEFAULT:
    default:
        return OVD_COLOR_DEFAULT;
    }
}

/* ---- SGR encoding ---------------------------------------------------- */

/* A sequence under construction. Capacity is OVD_COLOR_SGR_MAX_LENGTH;
 * the appends below never exceed it (see the header's worst case), and
 * each one checks anyway, recording overflow instead of writing. */
typedef struct SgrWriter {
    char bytes[OVD_COLOR_SGR_MAX_LENGTH];
    size_t length;
    bool has_parameter;
    bool overflow;
} SgrWriter;

static void put_char(SgrWriter *writer, char c)
{
    if (writer->length >= sizeof writer->bytes) {
        writer->overflow = true;
        return;
    }
    writer->bytes[writer->length++] = c;
}

/* Appends one decimal parameter (0-255), preceded by ESC [ or ';'. */
static void put_parameter(SgrWriter *writer, uint8_t value)
{
    if (writer->has_parameter) {
        put_char(writer, ';');
    } else {
        put_char(writer, '\x1b');
        put_char(writer, '[');
        writer->has_parameter = true;
    }
    if (value >= 100) {
        put_char(writer, (char)('0' + value / 100));
    }
    if (value >= 10) {
        put_char(writer, (char)('0' + (value / 10) % 10));
    }
    put_char(writer, (char)('0' + value % 10));
}

static void put_attributes(SgrWriter *writer, uint16_t attrs)
{
    static const struct {
        uint16_t attr;
        uint8_t code;
    } codes[] = {
        { OVD_ATTR_BOLD, 1 },      { OVD_ATTR_DIM, 2 },     { OVD_ATTR_ITALIC, 3 },
        { OVD_ATTR_UNDERLINE, 4 }, { OVD_ATTR_REVERSE, 7 }, { OVD_ATTR_STRIKETHROUGH, 9 },
    };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; ++i) {
        if ((attrs & codes[i].attr) != 0) {
            put_parameter(writer, codes[i].code);
        }
    }
}

/* Appends a downsampled color. base is 30 (foreground) or 40 (background):
 * default is base + 9, indexed 0-7 base + i, 8-15 base + 60 + (i - 8),
 * 16-255 base + 8;5;i, RGB base + 8;2;r;g;b. */
static void put_color(SgrWriter *writer, OvdColor color, uint8_t base)
{
    switch (color.kind) {
    case OVD_COLOR_KIND_INDEXED:
        if (color.index < 8) {
            put_parameter(writer, (uint8_t)(base + color.index));
        } else if (color.index < 16) {
            put_parameter(writer, (uint8_t)(base + 60 + (color.index - 8)));
        } else {
            put_parameter(writer, (uint8_t)(base + 8));
            put_parameter(writer, 5);
            put_parameter(writer, color.index);
        }
        return;
    case OVD_COLOR_KIND_RGB:
        put_parameter(writer, (uint8_t)(base + 8));
        put_parameter(writer, 2);
        put_parameter(writer, color.r);
        put_parameter(writer, color.g);
        put_parameter(writer, color.b);
        return;
    case OVD_COLOR_KIND_DEFAULT:
    default:
        put_parameter(writer, (uint8_t)(base + 9));
        return;
    }
}

static bool color_equal(OvdColor a, OvdColor b)
{
    return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

bool ovd_color_sgr_encode(OvdStyle from, OvdStyle to, OvdColorDepth depth, char *buffer,
                          size_t capacity, size_t *length)
{
    if (length == nullptr) {
        return false;
    }
    const uint16_t from_attrs = (uint16_t)(from.attrs & OVD_COLOR_ATTR_MASK);
    const uint16_t to_attrs = (uint16_t)(to.attrs & OVD_COLOR_ATTR_MASK);
    const OvdColor from_fg = ovd_color_downsample(from.fg, depth);
    const OvdColor from_bg = ovd_color_downsample(from.bg, depth);
    const OvdColor to_fg = ovd_color_downsample(to.fg, depth);
    const OvdColor to_bg = ovd_color_downsample(to.bg, depth);

    SgrWriter writer = { .length = 0 };
    if ((from_attrs & ~to_attrs) != 0) {
        /* An attribute was removed; R7 has no per-attribute off codes. */
        put_parameter(&writer, 0);
        put_attributes(&writer, to_attrs);
        if (to_fg.kind != OVD_COLOR_KIND_DEFAULT) {
            put_color(&writer, to_fg, 30);
        }
        if (to_bg.kind != OVD_COLOR_KIND_DEFAULT) {
            put_color(&writer, to_bg, 40);
        }
    } else {
        put_attributes(&writer, (uint16_t)(to_attrs & ~from_attrs));
        if (!color_equal(from_fg, to_fg)) {
            put_color(&writer, to_fg, 30);
        }
        if (!color_equal(from_bg, to_bg)) {
            put_color(&writer, to_bg, 40);
        }
    }
    if (writer.has_parameter) {
        put_char(&writer, 'm');
    }

    if (writer.overflow || (writer.length > 0 && (buffer == nullptr || capacity < writer.length))) {
        return false;
    }
    if (writer.length > 0) {
        memcpy(buffer, writer.bytes, writer.length);
    }
    *length = writer.length;
    return true;
}
