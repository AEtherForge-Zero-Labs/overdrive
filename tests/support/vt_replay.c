/* Test-only replay interpreter; see vt_replay.h and research R7, R33, R34, R37. */
#include "vt_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : size_t {
    ERROR_CAPACITY = 320,
    SHOWN_BYTES = 48,    /* offending bytes quoted in an error */
    MAX_PARAMS = 32,     /* SGR with every attribute and two RGB colors needs 17 */
    MAX_DIGITS = 5,
};

static const unsigned char escape = 0x1B;

struct VtReplay {
    TestScreen *screen;
    TestClassify classify;
    VtReplayState state;
    size_t consumed; /* bytes in earlier feeds, for error offsets */
    bool failed;
    char error[ERROR_CAPACITY];
};

/* Per-feed grapheme state: extenders attach only to the glyph printed
 * immediately before them in the same write. */
typedef struct Feed {
    const unsigned char *bytes;
    size_t length;
    bool can_extend;
    int32_t last_x;
    int32_t last_y;
    bool join_next;
} Feed;

VtReplay *vt_replay_create(int32_t cols, int32_t rows, TestClassify classify)
{
    if (classify == nullptr) {
        return nullptr;
    }
    VtReplay *vt = malloc(sizeof *vt);
    if (vt == nullptr) {
        return nullptr;
    }
    vt->screen = test_screen_create(cols, rows);
    if (vt->screen == nullptr) {
        free(vt);
        return nullptr;
    }
    vt->classify = classify;
    vt->state = (VtReplayState){
        .cursor_visible = true,
        .autowrap = true,
        .style = OVD_STYLE_DEFAULT,
    };
    vt->consumed = 0;
    vt->failed = false;
    vt->error[0] = '\0';
    return vt;
}

void vt_replay_destroy(VtReplay *vt)
{
    if (vt != nullptr) {
        test_screen_destroy(vt->screen);
        free(vt);
    }
}

const char *vt_replay_error(const VtReplay *vt)
{
    return vt == nullptr ? "" : vt->error;
}

const TestScreen *vt_replay_screen(const VtReplay *vt)
{
    return vt == nullptr ? nullptr : vt->screen;
}

VtReplayState vt_replay_state(const VtReplay *vt)
{
    return vt == nullptr ? (VtReplayState){ 0 } : vt->state;
}

/* Records the first error: "byte N: what: "escaped bytes"". Printable
 * ASCII is shown as is; every other byte as \xNN. */
static bool reject(VtReplay *vt, const Feed *feed, size_t start, size_t end, const char *what)
{
    char shown[SHOWN_BYTES * 4 + 4];
    size_t used = 0;
    const size_t stop = end - start > SHOWN_BYTES ? start + SHOWN_BYTES : end;
    for (size_t i = start; i < stop; ++i) {
        const unsigned char byte = feed->bytes[i];
        if (byte >= 0x20 && byte < 0x7F && byte != '"' && byte != '\\') {
            shown[used++] = (char)byte;
        } else {
            snprintf(shown + used, sizeof shown - used, "\\x%02x", (unsigned)byte);
            used += 4;
        }
    }
    if (stop < end) {
        memcpy(shown + used, "...", 3);
        used += 3;
    }
    shown[used] = '\0';
    snprintf(vt->error, sizeof vt->error, "byte %lu: %s: \"%s\"",
             (unsigned long)(vt->consumed + start), what, shown);
    vt->failed = true;
    return false;
}

static bool print_glyph(VtReplay *vt, Feed *feed, size_t at, size_t size, uint8_t width)
{
    VtReplayState *state = &vt->state;
    const int32_t cols = test_screen_cols(vt->screen);
    if (!state->cursor_known) {
        return reject(vt, feed, at, at + size,
                      "glyph printed while the cursor position is unknown (no CUP yet)");
    }
    if (state->pending_wrap) {
        return reject(vt, feed, at, at + size,
                      "glyph printed in the pending-wrap state (R33)");
    }
    if (width == 2 && state->x == cols - 1) {
        return reject(vt, feed, at, at + size,
                      "wide glyph starts in the last column and would wrap (R33)");
    }
    test_screen_put(vt->screen, state->x, state->y, (const char *)feed->bytes + at, size,
                    width, state->style);
    feed->can_extend = true;
    feed->last_x = state->x;
    feed->last_y = state->y;
    if (state->x + width == cols) {
        state->x = cols - 1;
        state->pending_wrap = true;
    } else {
        state->x += width;
    }
    return true;
}

static bool text(VtReplay *vt, Feed *feed, size_t at, size_t *next)
{
    uint32_t code_point = 0;
    const size_t size = test_utf8_decode(feed->bytes + at, feed->length - at, &code_point);
    if (size == 0) {
        const size_t end = feed->length - at > 4 ? at + 4 : feed->length;
        return reject(vt, feed, at, at + 1 < end ? end : at + 1, "invalid UTF-8");
    }
    *next = at + size;
    if (test_code_point_forbidden(code_point)) {
        char what[96];
        snprintf(what, sizeof what,
                 "U+%04lX is a control, line/paragraph separator, or bidi control",
                 (unsigned long)code_point);
        return reject(vt, feed, at, at + size, what);
    }
    const bool extends = feed->join_next || code_point == TEST_ZWJ
        || vt->classify(code_point) == TEST_WIDTH_EXTEND;
    if (!extends) {
        const uint8_t width = vt->classify(code_point) == TEST_WIDTH_WIDE ? 2 : 1;
        return print_glyph(vt, feed, at, size, width);
    }
    if (!feed->can_extend) {
        return reject(vt, feed, at, at + size,
                      "extender with no glyph printed just before it in this write");
    }
    if (!test_screen_extend(vt->screen, feed->last_x, feed->last_y,
                            (const char *)feed->bytes + at, size)) {
        return reject(vt, feed, at, at + size, "grapheme longer than 15 bytes");
    }
    feed->join_next = code_point == TEST_ZWJ;
    return true;
}

/* Parses "p;p;...;p" (each a decimal without leading zeros, at most five
 * digits). An empty string is zero parameters. */
static bool parse_params(const unsigned char *bytes, size_t length,
                         uint32_t params[MAX_PARAMS], size_t *count)
{
    *count = 0;
    if (length == 0) {
        return true;
    }
    size_t i = 0;
    for (;;) {
        size_t digits = 0;
        uint32_t value = 0;
        while (i < length && bytes[i] >= '0' && bytes[i] <= '9') {
            if (digits == MAX_DIGITS || (digits == 1 && value == 0)) {
                return false;
            }
            value = value * 10 + (uint32_t)(bytes[i] - '0');
            ++digits;
            ++i;
        }
        if (digits == 0 || *count == MAX_PARAMS) {
            return false;
        }
        params[(*count)++] = value;
        if (i == length) {
            return true;
        }
        if (bytes[i] != ';') {
            return false;
        }
        ++i;
    }
}

static bool color_param(const uint32_t params[], size_t count, size_t *i, OvdColor *out)
{
    if (*i + 1 >= count) {
        return false;
    }
    const uint32_t form = params[*i + 1];
    if (form == 5 && *i + 2 < count && params[*i + 2] <= 255) {
        *out = ovd_color_indexed((uint8_t)params[*i + 2]);
        *i += 2;
        return true;
    }
    if (form == 2 && *i + 4 < count && params[*i + 2] <= 255 && params[*i + 3] <= 255
            && params[*i + 4] <= 255) {
        *out = ovd_color_rgb((uint8_t)params[*i + 2], (uint8_t)params[*i + 3],
                             (uint8_t)params[*i + 4]);
        *i += 4;
        return true;
    }
    return false;
}

/* Applies SGR parameters to a copy of the style; false if any is outside R7. */
static bool apply_sgr(OvdStyle *style, const uint32_t params[], size_t count)
{
    OvdStyle next = *style;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t p = params[i];
        switch (p) {
        case 0: next = OVD_STYLE_DEFAULT; break;
        case 1: next.attrs |= OVD_ATTR_BOLD; break;
        case 2: next.attrs |= OVD_ATTR_DIM; break;
        case 3: next.attrs |= OVD_ATTR_ITALIC; break;
        case 4: next.attrs |= OVD_ATTR_UNDERLINE; break;
        case 7: next.attrs |= OVD_ATTR_REVERSE; break;
        case 9: next.attrs |= OVD_ATTR_STRIKETHROUGH; break;
        case 38:
            if (!color_param(params, count, &i, &next.fg)) {
                return false;
            }
            break;
        case 48:
            if (!color_param(params, count, &i, &next.bg)) {
                return false;
            }
            break;
        case 39: next.fg = OVD_COLOR_DEFAULT; break;
        case 49: next.bg = OVD_COLOR_DEFAULT; break;
        default:
            if (p >= 30 && p <= 37) {
                next.fg = ovd_color_indexed((uint8_t)(p - 30));
            } else if (p >= 90 && p <= 97) {
                next.fg = ovd_color_indexed((uint8_t)(p - 90 + 8));
            } else if (p >= 40 && p <= 47) {
                next.bg = ovd_color_indexed((uint8_t)(p - 40));
            } else if (p >= 100 && p <= 107) {
                next.bg = ovd_color_indexed((uint8_t)(p - 100 + 8));
            } else {
                return false;
            }
            break;
        }
    }
    *style = next;
    return true;
}

static bool style_is_default(OvdStyle style)
{
    return style.fg.kind == OVD_COLOR_KIND_DEFAULT && style.bg.kind == OVD_COLOR_KIND_DEFAULT
        && style.attrs == OVD_ATTR_NONE;
}

static bool private_mode(VtReplay *vt, Feed *feed, size_t at, size_t end,
                         const unsigned char *params, size_t length, unsigned char final)
{
    uint32_t values[MAX_PARAMS];
    size_t count = 0;
    if ((final != 'h' && final != 'l') || !parse_params(params, length, values, &count)
            || count != 1) {
        return reject(vt, feed, at, end, "unsupported DEC private mode sequence");
    }
    const bool set = final == 'h';
    VtReplayState *state = &vt->state;
    switch (values[0]) {
    case 25:
        state->cursor_visible = set;
        return true;
    case 7:
        state->autowrap = set;
        return true;
    case 1049:
        state->alternate_screen = set;
        if (set) {
            test_screen_clear(vt->screen);
        } else {
            /* The primary screen is not modeled. */
            test_screen_forget(vt->screen);
            state->cursor_known = false;
            state->pending_wrap = false;
        }
        return true;
    case 2026:
        if (state->synchronized == set) {
            return reject(vt, feed, at, end,
                          set ? "mode 2026 begin while a synchronized update is open"
                              : "mode 2026 end without a synchronized update open");
        }
        state->synchronized = set;
        return true;
    default:
        return reject(vt, feed, at, end, "unsupported DEC private mode");
    }
}

/* Applies the escape sequence starting at feed->bytes[at] (an ESC). */
static bool sequence(VtReplay *vt, Feed *feed, size_t at, size_t *next)
{
    const unsigned char *bytes = feed->bytes;
    const size_t length = feed->length;
    if (at + 1 >= length) {
        return reject(vt, feed, at, length, "truncated escape sequence");
    }
    if (bytes[at + 1] != '[') {
        return reject(vt, feed, at, at + 2, "unsupported escape sequence");
    }
    /* ECMA-48 5.4: parameter bytes 0x30-0x3F, intermediates 0x20-0x2F, final 0x40-0x7E. */
    size_t i = at + 2;
    while (i < length && bytes[i] >= 0x30 && bytes[i] <= 0x3F) {
        ++i;
    }
    const size_t params_end = i;
    while (i < length && bytes[i] >= 0x20 && bytes[i] <= 0x2F) {
        ++i;
    }
    if (i >= length) {
        return reject(vt, feed, at, length, "truncated control sequence");
    }
    const unsigned char final = bytes[i];
    const size_t end = i + 1;
    *next = end;
    if (final < 0x40 || final > 0x7E) {
        return reject(vt, feed, at, end, "malformed control sequence");
    }
    if (params_end != i) {
        return reject(vt, feed, at, end, "unsupported control sequence (intermediate bytes)");
    }
    const unsigned char *params = bytes + at + 2;
    size_t params_length = params_end - (at + 2);
    if (params_length > 0 && params[0] == '?') {
        return private_mode(vt, feed, at, end, params + 1, params_length - 1, final);
    }
    uint32_t values[MAX_PARAMS];
    size_t count = 0;
    if (!parse_params(params, params_length, values, &count)) {
        return reject(vt, feed, at, end, "unsupported control sequence parameters");
    }
    VtReplayState *state = &vt->state;
    switch (final) {
    case 'H':
        if (count != 2 || values[0] < 1 || values[1] < 1
                || values[0] > (uint32_t)test_screen_rows(vt->screen)
                || values[1] > (uint32_t)test_screen_cols(vt->screen)) {
            return reject(vt, feed, at, end,
                          "CUP needs two explicit parameters inside the screen");
        }
        state->cursor_known = true;
        state->y = (int32_t)values[0] - 1;
        state->x = (int32_t)values[1] - 1;
        state->pending_wrap = false;
        feed->can_extend = false;
        return true;
    case 'J':
        if (count != 1 || values[0] != 2) {
            return reject(vt, feed, at, end, "unsupported erase (only ED 2)");
        }
        if (!style_is_default(state->style)) {
            return reject(vt, feed, at, end,
                          "ED 2 outside the default SGR state (erase color is terminal-dependent)");
        }
        test_screen_clear(vt->screen);
        feed->can_extend = false;
        return true;
    case 'm':
        if (count == 0 || !apply_sgr(&state->style, values, count)) {
            return reject(vt, feed, at, end, "unsupported SGR");
        }
        feed->can_extend = false;
        return true;
    default:
        return reject(vt, feed, at, end, "unsupported control sequence");
    }
}

bool vt_replay_feed(VtReplay *vt, const void *bytes, size_t length)
{
    if (vt == nullptr || vt->failed) {
        return false;
    }
    if (bytes == nullptr) {
        return length == 0;
    }
    Feed feed = { .bytes = bytes, .length = length };
    size_t i = 0;
    while (i < length) {
        size_t next = i + 1;
        bool ok = false;
        if (feed.bytes[i] == escape) {
            if (feed.join_next) {
                return reject(vt, &feed, i, i + 1, "ZWJ joins nothing (followed by a sequence)");
            }
            ok = sequence(vt, &feed, i, &next);
        } else {
            ok = text(vt, &feed, i, &next);
        }
        if (!ok) {
            return false;
        }
        i = next;
    }
    if (feed.join_next) {
        return reject(vt, &feed, length, length, "ZWJ at the end of a write joins nothing");
    }
    vt->consumed += length;
    return true;
}
