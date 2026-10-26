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
 * allocation-free. Only the fields named by kind are meaningful. Build
 * values with the functions below rather than literals. */
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
    uint16_t attrs; /* OvdAttr bits; undefined bits are ignored */
} OvdStyle;

#define OVD_STYLE_DEFAULT \
    ((OvdStyle){ .fg = OVD_COLOR_DEFAULT, .bg = OVD_COLOR_DEFAULT, .attrs = OVD_ATTR_NONE })

/* ---- Cells ----------------------------------------------------------- */

/* A read-only view of one cell. text is one grapheme in UTF-8,
 * NUL-terminated, valid until the canvas is next drawn on or the frame
 * ends. A wide grapheme occupies two cells: the first has width 2, the
 * second width 0 and empty text. */
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
 * to the whole canvas. It is owned by the terminal and valid until
 * ovd_frame_end or ovd_frame_abandon.
 *
 * Fails with OVD_ERROR_STATE if a frame is open, OVD_ERROR_ALLOCATION if
 * a resize needed memory that was refused, or OVD_ERROR_PLATFORM if the
 * size could not be read. */
[[nodiscard]] OVD_API OvdResult ovd_frame_begin(OvdTerminal *terminal, OvdCanvas **out);

/* Writes the difference between this frame and the screen, in a single
 * write, and closes the frame. A frame identical to the screen writes
 * nothing. Fails with OVD_ERROR_STATE if no frame is open, or
 * OVD_ERROR_IO; the frame is closed either way, and after OVD_ERROR_IO
 * the next frame is a full redraw. */
[[nodiscard]] OVD_API OvdResult ovd_frame_end(OvdTerminal *terminal);

/* Closes the open frame without writing anything. Does nothing if no
 * frame is open. */
OVD_API void ovd_frame_abandon(OvdTerminal *terminal);

/* ---- Drawing --------------------------------------------------------- */
/* Drawing never fails and never allocates. Anything outside the clip
 * rectangle is discarded. canvas must be the open frame's canvas. */

OVD_API OvdSize ovd_canvas_size(const OvdCanvas *canvas);

OVD_API OvdRect ovd_canvas_clip(const OvdCanvas *canvas);

/* Sets the clip to clip intersected with the canvas bounds and returns
 * the previous clip, so callers can restore it. */
OVD_API OvdRect ovd_canvas_set_clip(OvdCanvas *canvas, OvdRect clip);

/* Sets every cell of area to a space in style. */
OVD_API void ovd_canvas_fill(OvdCanvas *canvas, OvdRect area, OvdStyle style);

/* Draws length bytes of UTF-8 on row y starting at column x, one
 * grapheme per cell (two for wide graphemes), and returns the number of
 * columns the text spans, clipped or not.
 *
 * Text is untrusted. Invalid UTF-8, control characters (C0, DEL, C1,
 * including newline and tab), and graphemes too long to store are each
 * drawn as U+FFFD; nothing in text reaches the terminal as a control
 * sequence. A wide grapheme that would be cut by the clip or the right
 * edge is drawn as a space in style. Drawing over half of an existing
 * wide grapheme turns its other half into a space. */
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

/* Draws the outline of area. Areas narrower or shorter than 2 cells draw
 * nothing. The interior is left as it was. */
OVD_API void ovd_canvas_box(OvdCanvas *canvas, OvdRect area, OvdBorder border, OvdStyle style);

#endif /* OVERDRIVE_RENDER_H */
