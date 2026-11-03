#include "diff.h"

#include "color.h"

#include <stdckdint.h>
#include <stdint.h>
#include <string.h>

static constexpr char SYNC_ON[] = "\x1b[?2026h";
static constexpr char SYNC_OFF[] = "\x1b[?2026l";
static constexpr char RESET[] = "\x1b[0m";
static constexpr char CLEAR[] = "\x1b[2J";
static constexpr char ZWJ[] = "\xE2\x80\x8D"; /* U+200D in UTF-8 */

/* A default space, what CSI 2 J leaves in every cell. OVD_STYLE_DEFAULT
 * is all zero fields. */
static const OvdCell BLANK = { .text = " ", .length = 1, .width = 1 };

static bool color_equal(OvdColor a, OvdColor b)
{
    return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

/* Field by field: OvdStyle has padding, and text past length is not
 * part of the cell. */
static bool cell_equal(const OvdCell *a, const OvdCell *b)
{
    if (a->length != b->length || a->width != b->width || a->style.attrs != b->style.attrs
        || !color_equal(a->style.fg, b->style.fg) || !color_equal(a->style.bg, b->style.bg)) {
        return false;
    }
    for (size_t i = 0; i < a->length; ++i) {
        if (a->text[i] != b->text[i]) {
            return false;
        }
    }
    return true;
}

static size_t digits(int32_t value)
{
    size_t count = 1;
    while (value >= 10) {
        value /= 10;
        ++count;
    }
    return count;
}

/* What the terminal is known to have, as the output is built. */
typedef struct Writer {
    OvdOutput *output;
    OvdColorDepth depth;
    bool failed;
    bool cursor_known;
    int32_t x;
    int32_t y;
    OvdStyle style;
} Writer;

static void put(Writer *w, const char *bytes, size_t length)
{
    if (!w->failed && !ovd_output_append(w->output, bytes, length)) {
        w->failed = true;
    }
}

/* Appends value in decimal; value is a 1-based coordinate. */
static void put_number(Writer *w, int32_t value)
{
    char text[10];
    size_t length = 0;
    do {
        text[sizeof text - 1 - length] = (char)('0' + value % 10);
        value /= 10;
        ++length;
    } while (value > 0);
    put(w, text + sizeof text - length, length);
}

static void move_to(Writer *w, int32_t x, int32_t y)
{
    if (w->cursor_known && w->x == x && w->y == y) {
        return;
    }
    put(w, "\x1b[", 2);
    put_number(w, y + 1);
    put(w, ";", 1);
    put_number(w, x + 1);
    put(w, "H", 1);
    w->cursor_known = true;
    w->x = x;
    w->y = y;
}

/* Writes the lead cell at (x, y). After it the cursor is x + width,
 * which equals cols after the last column, so it never matches a cell
 * there and the next glyph gets a CUP (R33). After text ending in ZWJ
 * the cursor counts as unknown (R38). */
static void write_cell(Writer *w, const OvdCell *cell, int32_t x, int32_t y)
{
    move_to(w, x, y);
    char sgr[OVD_COLOR_SGR_MAX_LENGTH];
    size_t length = 0;
    if (!ovd_color_sgr_encode(w->style, cell->style, w->depth, sgr, sizeof sgr, &length)) {
        w->failed = true;
        return;
    }
    put(w, sgr, length);
    w->style = cell->style;
    put(w, cell->text, cell->length);
    w->x += cell->width;
    if (cell->length >= sizeof ZWJ - 1
        && memcmp(cell->text + cell->length - (sizeof ZWJ - 1), ZWJ, sizeof ZWJ - 1) == 0) {
        w->cursor_known = false;
    }
}

/* The start of a non-empty output (R34): synchronized-output on, then
 * the full-redraw prefix when the screen is unknown. */
static void begin(Writer *w, bool synchronized, bool full)
{
    if (synchronized) {
        put(w, SYNC_ON, sizeof SYNC_ON - 1);
    }
    if (full) {
        put(w, RESET, sizeof RESET - 1);
        put(w, CLEAR, sizeof CLEAR - 1);
    }
}

OvdResult ovd_diff_render(const OvdBuffer *screen, const OvdBuffer *frame,
                          OvdColorDepth depth, bool synchronized, OvdOutput *output)
{
    if (frame == nullptr || output == nullptr || depth > OVD_COLOR_DEPTH_TRUECOLOR) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    const OvdSize size = ovd_buffer_size(frame);
    if (size.cols <= 0 || size.rows <= 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (screen != nullptr) {
        const OvdSize shown = ovd_buffer_size(screen);
        if (shown.cols != size.cols || shown.rows != size.rows) {
            return OVD_ERROR_INVALID_ARGUMENT;
        }
    }

    Writer w = { .output = output, .depth = depth, .style = OVD_STYLE_DEFAULT };
    /* An unknown screen is always redrawn, even to a blank frame. */
    bool started = screen == nullptr;
    if (started) {
        begin(&w, synchronized, true);
    }
    for (int32_t y = 0; y < size.rows; ++y) {
        for (int32_t x = 0; x < size.cols; ++x) {
            const OvdCell *cell = ovd_buffer_cell(frame, x, y);
            /* Only leads are compared. A continuation is written with
             * its lead and equals the screen's whenever its lead does:
             * both are width 0, empty, in the lead's style (buffer.h).
             * Width is compared, so a change that splits a wide glyph on
             * screen also changes the cell holding its other half. */
            if (cell->width == 0) {
                continue;
            }
            const OvdCell *old = screen == nullptr ? &BLANK : ovd_buffer_cell(screen, x, y);
            if (cell_equal(cell, old)) {
                continue;
            }
            if (!started) {
                begin(&w, synchronized, false);
                started = true;
            }
            write_cell(&w, cell, x, y);
        }
    }
    if (started) {
        put(&w, RESET, sizeof RESET - 1);
        if (synchronized) {
            put(&w, SYNC_OFF, sizeof SYNC_OFF - 1);
        }
    }
    if (w.failed) {
        ovd_output_reset(output);
        return OVD_ERROR_IO;
    }
    return OVD_OK;
}

OvdResult ovd_diff_bound(OvdSize size, bool synchronized, size_t *bytes)
{
    if (bytes == nullptr || size.cols <= 0 || size.rows <= 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    const size_t cup = 4 + digits(size.rows) + digits(size.cols);
    const size_t per_cell = cup + OVD_COLOR_SGR_MAX_LENGTH + OVD_GRAPHEME_MAX_BYTES;
    const size_t fixed = (synchronized ? (sizeof SYNC_ON - 1) + (sizeof SYNC_OFF - 1) : 0)
                         + (sizeof RESET - 1) + (sizeof CLEAR - 1) + (sizeof RESET - 1);
    size_t cells;
    size_t total;
    if (ckd_mul(&cells, (size_t)size.cols, (size_t)size.rows)
        || ckd_mul(&total, cells, per_cell) || ckd_add(&total, total, fixed)) {
        return OVD_ERROR_ALLOCATION;
    }
    *bytes = total;
    return OVD_OK;
}
