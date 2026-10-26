/* Test-only screen model; see screen.h. */
#include "screen.h"

#include <stdckdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TestScreen {
    int32_t cols;
    int32_t rows;
    TestCell *cells; /* rows * cols, row-major */
};


static TestCell *cell_at(TestScreen *screen, int32_t x, int32_t y)
{
    return &screen->cells[(size_t)y * (size_t)screen->cols + (size_t)x];
}

static TestCell blank_cell(void)
{
    return (TestCell){ .text = " ", .length = 1, .width = 1, .style = OVD_STYLE_DEFAULT };
}

TestScreen *test_screen_create(int32_t cols, int32_t rows)
{
    if (cols <= 0 || rows <= 0) {
        return nullptr;
    }
    size_t count = 0;
    size_t bytes = 0;
    if (ckd_mul(&count, (size_t)cols, (size_t)rows)
            || ckd_mul(&bytes, count, sizeof(TestCell))) {
        return nullptr;
    }
    TestScreen *screen = malloc(sizeof *screen);
    if (screen == nullptr) {
        return nullptr;
    }
    screen->cells = malloc(bytes);
    if (screen->cells == nullptr) {
        free(screen);
        return nullptr;
    }
    screen->cols = cols;
    screen->rows = rows;
    test_screen_clear(screen);
    return screen;
}

void test_screen_destroy(TestScreen *screen)
{
    if (screen != nullptr) {
        free(screen->cells);
        free(screen);
    }
}

int32_t test_screen_cols(const TestScreen *screen)
{
    return screen == nullptr ? 0 : screen->cols;
}

int32_t test_screen_rows(const TestScreen *screen)
{
    return screen == nullptr ? 0 : screen->rows;
}

const TestCell *test_screen_cell(const TestScreen *screen, int32_t x, int32_t y)
{
    if (screen == nullptr || x < 0 || y < 0 || x >= screen->cols || y >= screen->rows) {
        return nullptr;
    }
    return &screen->cells[(size_t)y * (size_t)screen->cols + (size_t)x];
}

void test_screen_clear(TestScreen *screen)
{
    const size_t count = (size_t)screen->cols * (size_t)screen->rows;
    for (size_t i = 0; i < count; ++i) {
        screen->cells[i] = blank_cell();
    }
}

void test_screen_forget(TestScreen *screen)
{
    const size_t count = (size_t)screen->cols * (size_t)screen->rows;
    for (size_t i = 0; i < count; ++i) {
        screen->cells[i] = blank_cell();
        screen->cells[i].indeterminate = true;
    }
}

/* The surviving half of a split wide glyph: what it shows is up to the
 * terminal, so the model refuses to vouch for it. */
static void orphan(TestCell *cell)
{
    const OvdStyle style = cell->style;
    *cell = blank_cell();
    cell->style = style;
    cell->indeterminate = true;
}

void test_screen_put(TestScreen *screen, int32_t x, int32_t y,
                     const char *text, size_t length, uint8_t width, OvdStyle style)
{
    const int32_t end = x + width; /* exclusive; the caller guarantees end <= cols */
    for (int32_t c = x; c < end; ++c) {
        const TestCell *cell = cell_at(screen, c, y);
        if (cell->width == 0 && c - 1 < x) {
            orphan(cell_at(screen, c - 1, y));
        } else if (cell->width == 2 && c + 1 >= end) {
            orphan(cell_at(screen, c + 1, y));
        }
    }
    TestCell *lead = cell_at(screen, x, y);
    *lead = (TestCell){ .length = (uint8_t)length, .width = width, .style = style };
    memcpy(lead->text, text, length);
    lead->text[length] = '\0';
    if (width == 2) {
        *cell_at(screen, x + 1, y) = (TestCell){ .width = 0, .style = style };
    }
}

bool test_screen_extend(TestScreen *screen, int32_t x, int32_t y,
                        const char *bytes, size_t length)
{
    TestCell *cell = cell_at(screen, x, y);
    if (cell->width == 0 || cell->indeterminate
            || length > TEST_GRAPHEME_MAX - cell->length) {
        return false;
    }
    memcpy(cell->text + cell->length, bytes, length);
    cell->length = (uint8_t)(cell->length + length);
    cell->text[cell->length] = '\0';
    return true;
}

/* Unicode Standard, Chapter 3, Table 3-7 (well-formed UTF-8 byte sequences). */
size_t test_utf8_decode(const unsigned char *bytes, size_t length, uint32_t *out)
{
    if (length == 0) {
        return 0;
    }
    const unsigned char lead = bytes[0];
    size_t need = 0;
    uint32_t value = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead < 0x80) {
        *out = lead;
        return 1;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
        need = 1;
        value = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        need = 2;
        value = lead & 0x0Fu;
        low = lead == 0xE0 ? 0xA0 : 0x80;
        high = lead == 0xED ? 0x9F : 0xBF;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        need = 3;
        value = lead & 0x07u;
        low = lead == 0xF0 ? 0x90 : 0x80;
        high = lead == 0xF4 ? 0x8F : 0xBF;
    } else {
        return 0;
    }
    if (length <= need) {
        return 0;
    }
    for (size_t i = 1; i <= need; ++i) {
        const unsigned char byte = bytes[i];
        if (byte < (i == 1 ? low : 0x80) || byte > (i == 1 ? high : 0xBF)) {
            return 0;
        }
        value = (value << 6) | (byte & 0x3Fu);
    }
    *out = value;
    return need + 1;
}

bool test_code_point_forbidden(uint32_t code_point)
{
    return code_point < 0x20 || (code_point >= 0x7F && code_point <= 0x9F)
        || code_point == 0x2028 || code_point == 0x2029
        || code_point == 0x061C || code_point == 0x200E || code_point == 0x200F
        || (code_point >= 0x202A && code_point <= 0x202E)
        || (code_point >= 0x2066 && code_point <= 0x2069);
}

static bool fail(char *error, size_t capacity, const char *what, int32_t row, int32_t column)
{
    if (capacity > 0) {
        snprintf(error, capacity, "row %d column %d: %s", (int)row, (int)column, what);
    }
    return false;
}

/* Places text row y, one grapheme at a time. */
static bool place_row(TestScreen *screen, int32_t y, const char *row, TestClassify classify,
                      char *error, size_t capacity)
{
    const unsigned char *bytes = (const unsigned char *)row;
    const size_t length = strlen(row);
    int32_t column = 0;     /* next free column */
    int32_t lead = -1;      /* column of the grapheme being extended */
    bool join_next = false;
    size_t i = 0;
    while (i < length) {
        uint32_t code_point = 0;
        const size_t size = test_utf8_decode(bytes + i, length - i, &code_point);
        if (size == 0) {
            return fail(error, capacity, "invalid UTF-8", y, column);
        }
        if (test_code_point_forbidden(code_point)) {
            return fail(error, capacity, "control or bidi code point", y, column);
        }
        const bool extends = join_next || code_point == TEST_ZWJ
            || classify(code_point) == TEST_WIDTH_EXTEND;
        if (extends) {
            if (lead < 0) {
                return fail(error, capacity, "extender with no base before it", y, column);
            }
            if (!test_screen_extend(screen, lead, y, row + i, size)) {
                return fail(error, capacity, "grapheme longer than 15 bytes", y, lead);
            }
            join_next = code_point == TEST_ZWJ;
        } else {
            const uint8_t width = classify(code_point) == TEST_WIDTH_WIDE ? 2 : 1;
            if (column > screen->cols - width) {
                return fail(error, capacity, "text wider than the screen", y, column);
            }
            test_screen_put(screen, column, y, row + i, size, width, OVD_STYLE_DEFAULT);
            lead = column;
            column += width;
        }
        i += size;
    }
    if (join_next) {
        return fail(error, capacity, "ZWJ at the end joins nothing", y, lead);
    }
    return true;
}

static bool style_row(TestScreen *screen, int32_t y, const char *keys,
                      const TestStyleKey legend[], size_t legend_count,
                      char *error, size_t capacity)
{
    const size_t length = strlen(keys);
    if (length > (size_t)screen->cols) {
        return fail(error, capacity, "style row wider than the screen", y, screen->cols);
    }
    for (int32_t x = 0; x < screen->cols; ++x) {
        const char key = (size_t)x < length ? keys[x] : '.';
        TestCell *cell = cell_at(screen, x, y);
        if (cell->width == 0) {
            const char lead_key = (size_t)(x - 1) < length ? keys[x - 1] : '.';
            if (key != lead_key) {
                char what[96];
                snprintf(what, sizeof what, "wide glyph's columns have keys '%c' and '%c'",
                         lead_key, key);
                return fail(error, capacity, what, y, x - 1);
            }
            cell->style = cell_at(screen, x - 1, y)->style;
            continue;
        }
        if (key == '.') {
            cell->style = OVD_STYLE_DEFAULT;
            continue;
        }
        size_t k = 0;
        while (k < legend_count && legend[k].key != key) {
            ++k;
        }
        if (k == legend_count) {
            char what[64];
            snprintf(what, sizeof what, "style key '%c' is not in the legend", key);
            return fail(error, capacity, what, y, x);
        }
        cell->style = legend[k].style;
    }
    return true;
}

TestScreen *test_screen_from_rows(int32_t cols, int32_t rows, TestClassify classify,
                                  const char *const text[], const char *const styles[],
                                  const TestStyleKey legend[], size_t legend_count,
                                  char *error, size_t capacity)
{
    if (capacity > 0) {
        error[0] = '\0';
    }
    if (classify == nullptr || text == nullptr) {
        fail(error, capacity, "nullptr classify or text", 0, 0);
        return nullptr;
    }
    TestScreen *screen = test_screen_create(cols, rows);
    if (screen == nullptr) {
        fail(error, capacity, "invalid size or out of memory", 0, 0);
        return nullptr;
    }
    for (int32_t y = 0; y < rows; ++y) {
        const bool ok = text[y] != nullptr
            && place_row(screen, y, text[y], classify, error, capacity)
            && (styles == nullptr
                || (styles[y] != nullptr
                    && style_row(screen, y, styles[y], legend, legend_count, error, capacity)));
        if (!ok) {
            if (capacity > 0 && error[0] == '\0') {
                fail(error, capacity, "nullptr row", y, 0);
            }
            test_screen_destroy(screen);
            return nullptr;
        }
    }
    return screen;
}

static bool color_equal(OvdColor a, OvdColor b)
{
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
    case OVD_COLOR_KIND_INDEXED:
        return a.index == b.index;
    case OVD_COLOR_KIND_RGB:
        return a.r == b.r && a.g == b.g && a.b == b.b;
    default:
        return true;
    }
}

static bool cell_equal(const TestCell *a, const TestCell *b)
{
    return !a->indeterminate && !b->indeterminate && a->width == b->width
        && a->length == b->length && memcmp(a->text, b->text, a->length) == 0
        && color_equal(a->style.fg, b->style.fg) && color_equal(a->style.bg, b->style.bg)
        && a->style.attrs == b->style.attrs;
}

static void describe_color(char *out, size_t capacity, OvdColor color)
{
    switch (color.kind) {
    case OVD_COLOR_KIND_INDEXED:
        snprintf(out, capacity, "indexed(%u)", (unsigned)color.index);
        break;
    case OVD_COLOR_KIND_RGB:
        snprintf(out, capacity, "rgb(%u,%u,%u)", (unsigned)color.r, (unsigned)color.g,
                 (unsigned)color.b);
        break;
    default:
        snprintf(out, capacity, "default");
        break;
    }
}

static void describe_cell(char *out, size_t capacity, const TestCell *cell)
{
    if (cell->indeterminate) {
        snprintf(out, capacity, "indeterminate (half of a split wide glyph)");
        return;
    }
    char fg[24];
    char bg[24];
    describe_color(fg, sizeof fg, cell->style.fg);
    describe_color(bg, sizeof bg, cell->style.bg);
    snprintf(out, capacity, "\"%s\" width %u fg=%s bg=%s attrs=0x%02x", cell->text,
             (unsigned)cell->width, fg, bg, (unsigned)cell->style.attrs);
}

bool test_screen_equal(const TestScreen *expected, const TestScreen *actual,
                       char *message, size_t capacity)
{
    if (capacity > 0) {
        message[0] = '\0';
    }
    if (expected == nullptr || actual == nullptr) {
        if (capacity > 0) {
            snprintf(message, capacity, "nullptr screen");
        }
        return false;
    }
    if (expected->cols != actual->cols || expected->rows != actual->rows) {
        if (capacity > 0) {
            snprintf(message, capacity, "size %dx%d expected, %dx%d actual",
                     (int)expected->cols, (int)expected->rows, (int)actual->cols,
                     (int)actual->rows);
        }
        return false;
    }
    for (int32_t y = 0; y < expected->rows; ++y) {
        for (int32_t x = 0; x < expected->cols; ++x) {
            const TestCell *want = test_screen_cell(expected, x, y);
            const TestCell *got = test_screen_cell(actual, x, y);
            if (!cell_equal(want, got)) {
                if (capacity > 0) {
                    char a[128];
                    char b[128];
                    describe_cell(a, sizeof a, want);
                    describe_cell(b, sizeof b, got);
                    snprintf(message, capacity, "cell (%d, %d): expected %s; got %s",
                             (int)x, (int)y, a, b);
                }
                return false;
            }
        }
    }
    return true;
}

size_t test_screen_dump(const TestScreen *screen, char *out, size_t capacity)
{
    if (capacity == 0) {
        return 0;
    }
    size_t used = 0;
    const size_t limit = capacity - 1;
    for (int32_t y = 0; screen != nullptr && y < screen->rows; ++y) {
        for (int32_t x = 0; x <= screen->cols; ++x) {
            const char *piece = "\n";
            size_t length = 1;
            if (x < screen->cols) {
                const TestCell *cell = test_screen_cell(screen, x, y);
                piece = cell->indeterminate ? "?" : cell->text;
                length = cell->indeterminate ? 1 : cell->length;
            }
            if (length > limit - used) {
                out[used] = '\0';
                return used;
            }
            memcpy(out + used, piece, length);
            used += length;
        }
    }
    out[used] = '\0';
    return used;
}
