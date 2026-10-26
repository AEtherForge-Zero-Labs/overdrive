/*
 * Overdrive rendering: colors, styles, cells, frames, and drawing.
 *
 * PROPOSED CONTRACT (specs/001-render-core). Experimental for 0.x.
 *
 * A frame is a complete description of the screen. ovd_frame_begin hands
 * out a canvas cleared to the default style; the caller draws everything
 * that should be visible; ovd_frame_end compares it with what the
 * terminal shows and writes only the difference, in one write.
 */
#ifndef OVERDRIVE_RENDER_H
#define OVERDRIVE_RENDER_H

#include <overdrive/core.h>
#include <overdrive/terminal.h>

/* ---- Colors ---------------------------------------------------------- */

typedef enum OvdColorKind : uint8_t {
    OVD_COLOR_KIND_DEFAULT = 0, /* the terminal's own foreground/background */
    OVD_COLOR_KIND_INDEXED,     /* palette entry 0-255; 0-15 follow the user's theme */
    OVD_COLOR_KIND_RGB,         /* 24-bit color */
} OvdColorKind;

/* A transparent value type: copied per cell, so it must stay small and
 * allocation-free. Only the fields named by kind are meaningful; the canvas
 * stores colors with the other fields zeroed, so two colors that look the
 * same compare equal. An out-of-range kind is drawn as the default color.
 * Build values with the functions below rather than literals. */
typedef struct OvdColor {
    OvdColorKind kind;
    uint8_t index;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} OvdColor;

#define OVD_COLOR_DEFAULT ((OvdColor){ .kind = OVD_COLOR_KIND_DEFAULT })

static inline OvdColor ovd_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (OvdColor){ .kind = OVD_COLOR_KIND_RGB, .r = r, .g = g, .b = b };
}

/* 0xRRGGBB; bits above 24 are ignored. */
static inline OvdColor ovd_color_hex(uint32_t rgb)
{
    return ovd_color_rgb((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}

static inline OvdColor ovd_color_indexed(uint8_t index)
{
    return (OvdColor){ .kind = OVD_COLOR_KIND_INDEXED, .index = index };
}

/* ---- Styles ---------------------------------------------------------- */

/* Text attributes. Values are bits; a set of them is stored in
 * OvdStyle.attrs. Terminals that lack one ignore it. */
typedef enum OvdAttr : uint16_t {
    OVD_ATTR_NONE = 0,
    OVD_ATTR_BOLD = 1u << 0,
    OVD_ATTR_DIM = 1u << 1,
    OVD_ATTR_ITALIC = 1u << 2,
    OVD_ATTR_UNDERLINE = 1u << 3,
    OVD_ATTR_REVERSE = 1u << 4,
    OVD_ATTR_STRIKETHROUGH = 1u << 5,
} OvdAttr;

typedef struct OvdStyle {
    OvdColor fg;
    OvdColor bg;
    uint16_t attrs; /* OvdAttr bits; undefined bits are dropped when drawn */
} OvdStyle;

#define OVD_STYLE_DEFAULT \
    ((OvdStyle){ .fg = OVD_COLOR_DEFAULT, .bg = OVD_COLOR_DEFAULT, .attrs = OVD_ATTR_NONE })

/* ---- Cells ----------------------------------------------------------- */

/* A read-only view of one cell. text is one grapheme in UTF-8, at most
 * 15 bytes, NUL-terminated, valid until the canvas is next drawn on or the
 * frame ends. A wide grapheme occupies two cells: the first has width 2,
 * the second width 0 and empty text. The canvas never holds one half of a
 * wide grapheme without the other. */
typedef struct OvdCellView {
    const char *text;
    size_t length;
    uint8_t width;
    OvdStyle style;
} OvdCellView;

/* ---- Frames ---------------------------------------------------------- */

typedef struct OvdCanvas OvdCanvas;

/* Starts a frame. Re-reads the terminal size first; if it changed, the
 * frame has the new size and will be a full redraw. *out is a canvas of
 * the terminal's size, every cell a space in OVD_STYLE_DEFAULT, clip set
 * to the whole canvas. It is owned by the terminal and open until
 * ovd_frame_end or ovd_frame_abandon. Allocates only if the size differs
 * from the terminal's current buffers, and then allocates all new buffers
 * before releasing the old ones.
 *
 * Fails with OVD_ERROR_INVALID_ARGUMENT for nullptr arguments;
 * OVD_ERROR_STATE if a frame is open or the terminal's sink is running;
 * OVD_ERROR_ALLOCATION if a resize needed memory that was refused; or
 * OVD_ERROR_PLATFORM if the size could not be read or reads as 0. On
 * failure no frame is open and the terminal keeps its old buffers and size;
 * the next call tries again. */
[[nodiscard]] OVD_API OvdResult ovd_frame_begin(OvdTerminal *terminal, OvdCanvas **out);

/* Writes the difference between this frame and the screen, in a single
 * write, and closes the frame. Never allocates. A frame identical to the
 * screen writes nothing. Any other output ends with an SGR reset, never
 * prints a glyph while the cursor waits to wrap, never scrolls, and so
 * renders the same with autowrap on or off. With synchronized_output it is
 * wrapped in DEC mode 2026 markers.
 *
 * Fails with OVD_ERROR_INVALID_ARGUMENT for nullptr; OVD_ERROR_STATE if
 * no frame is open or the sink is running; or OVD_ERROR_IO if the output
 * could not be written. This is the one function that changes state on
 * failure: the frame is closed on every outcome, and after OVD_ERROR_IO the
 * screen is unknown, so the next frame is a full redraw. */
[[nodiscard]] OVD_API OvdResult ovd_frame_end(OvdTerminal *terminal);

/* Closes the open frame without writing anything. Does nothing if no
 * frame is open. */
OVD_API void ovd_frame_abandon(OvdTerminal *terminal);

/* ---- Drawing --------------------------------------------------------- */
/* Drawing never fails and never allocates, for any int32_t arguments.
 * Anything outside the clip rectangle is discarded, with one exception:
 * when a draw overwrites one half of a wide grapheme, the other half
 * becomes a space in its own style even if it lies outside the clip, so
 * the canvas never holds half a glyph.
 *
 * After its frame ends or is abandoned, the canvas is closed: drawing does
 * nothing and queries return zero values. Using it after
 * ovd_terminal_destroy is undefined. nullptr does nothing. */

OVD_API OvdSize ovd_canvas_size(const OvdCanvas *canvas);

OVD_API OvdRect ovd_canvas_clip(const OvdCanvas *canvas);

/* Sets the clip to clip intersected with the canvas bounds and returns
 * the previous clip, so callers can restore it. An empty intersection is
 * stored as {0, 0, 0, 0}. */
OVD_API OvdRect ovd_canvas_set_clip(OvdCanvas *canvas, OvdRect clip);

/* Sets every cell of area to a space in style. */
OVD_API void ovd_canvas_fill(OvdCanvas *canvas, OvdRect area, OvdStyle style);

/* Draws length bytes of UTF-8 on row y starting at column x, one
 * grapheme per cell (two for wide graphemes), and returns the number of
 * columns the text spans, clipped or not, saturating at INT32_MAX. A
 * nullptr text draws nothing and returns 0.
 *
 * A grapheme is a base code point plus any following combining marks
 * (Mn, Me), format characters (Cf, except bidi controls), variation
 * selectors, and, after a ZWJ, the next code point unless it is one of
 * the replaced kinds below. Its width is its
 * base's: 2 for East Asian Width W or F, otherwise 1; what follows the
 * base never changes it. Segmentation restarts with each call.
 *
 * Text is untrusted. Each of these is drawn as one U+FFFD of width 1:
 * every maximal invalid UTF-8 subsequence; C0 controls (including NUL,
 * newline, and tab), DEL, and C1 controls; U+2028 and U+2029; bidi
 * controls; an extender with no base before it in text; and a whole
 * grapheme longer than 15 bytes. Nothing in text reaches the terminal as a
 * control sequence. A wide grapheme with either half outside the clip or
 * the canvas has its inside half drawn as a space in style, and still
 * advances two columns. */
OVD_API int32_t ovd_canvas_text(OvdCanvas *canvas, int32_t x, int32_t y,
                                const char *text, size_t length, OvdStyle style);

/* Reads the cell at (x, y) as drawn so far in this frame. Returns false,
 * leaving *out untouched, if (x, y) is outside the canvas. */
OVD_API bool ovd_canvas_cell(const OvdCanvas *canvas, int32_t x, int32_t y,
                             OvdCellView *out);

/* ---- Boxes (User Story 3, P2) ---------------------------------------- */

typedef enum OvdBorder : uint8_t {
    OVD_BORDER_LIGHT = 0, /* ┌─┐ */
    OVD_BORDER_HEAVY,     /* ┏━┓ */
    OVD_BORDER_DOUBLE,    /* ╔═╗ */
    OVD_BORDER_ROUNDED,   /* ╭─╮ */
    OVD_BORDER_ASCII,     /* +-+ : for fonts without box-drawing glyphs */
} OvdBorder;

/* Draws the outline of area. Areas narrower or shorter than 2 cells, and
 * out-of-range border values, draw nothing. The interior is left as it
 * was. */
OVD_API void ovd_canvas_box(OvdCanvas *canvas, OvdRect area, OvdBorder border, OvdStyle style);

#endif /* OVERDRIVE_RENDER_H */
