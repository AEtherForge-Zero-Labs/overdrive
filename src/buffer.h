/*
 * Cell grid and canvas state (private). FR-030, FR-033; research.md R18
 * to R23, R31.
 *
 * Owning module of OvdBuffer (buffer.c) and of struct OvdCanvas
 * (canvas.c). Both structs are declared here only so the terminal can
 * embed them without separate allocations (R23); only their own source
 * file reads or changes their fields.
 */
#ifndef OVERDRIVE_SRC_BUFFER_H
#define OVERDRIVE_SRC_BUFFER_H

#include "alloc.h"
#include "unicode.h"

#include <overdrive/render.h>

#include <stddef.h>
#include <stdint.h>

/* One cell as the grid stores it: a plain value, read once per cell by
 * the renderer. text holds length bytes of one grapheme plus a NUL
 * (length <= OVD_GRAPHEME_MAX_BYTES). width is 1 for a narrow cell, 2 for
 * the first half of a wide grapheme, and 0 for its second half, whose
 * text is empty and whose style is the first half's. */
typedef struct OvdCell {
    char text[OVD_GRAPHEME_MAX_BYTES + 1];
    uint8_t length;
    uint8_t width;
    OvdStyle style;
} OvdCell;

/* A grid of size.cols x size.rows cells, row-major, in storage.
 *
 * Invariants, kept by every function below:
 * - empty ({ nullptr, 0 } storage) exactly when size is {0, 0}; a
 *   zero-initialized OvdBuffer is a valid empty grid;
 * - pairing: a cell of width 2 is always followed, in the same row, by
 *   a cell of width 0, and a cell of width 0 is always preceded by one
 *   of width 2. The grid never holds half a wide grapheme (FR-030, R20).
 */
typedef struct OvdBuffer {
    OvdBlock storage;
    OvdSize size;
} OvdBuffer;

/* Writes into *out the storage request for a grid of size, to pass to
 * ovd_alloc_all with the terminal's other requests (R31). The out array
 * given to ovd_alloc_all must hold no live blocks: every failure empties
 * it.
 *
 * OVD_ERROR_INVALID_ARGUMENT: out nullptr, or a dimension not positive.
 * OVD_ERROR_ALLOCATION: cols x rows overflows size_t. On failure *out is
 * unchanged. */
[[nodiscard]] OvdResult ovd_buffer_request(OvdSize size, OvdBlockRequest *out);

/* Releases the current storage (if any) through allocator, then takes
 * *storage, emptying it. storage must come from the same allocator, for
 * a request that ovd_buffer_request made for size. Every cell becomes a
 * space in OVD_STYLE_DEFAULT. */
void ovd_buffer_adopt(OvdBuffer *buffer, const OvdAllocator *allocator, OvdBlock *storage,
                      OvdSize size);

/* Releases the storage; the grid is then empty. */
void ovd_buffer_release(OvdBuffer *buffer, const OvdAllocator *allocator);

OvdSize ovd_buffer_size(const OvdBuffer *buffer);

/* Sets every cell to a space in OVD_STYLE_DEFAULT. */
void ovd_buffer_clear(OvdBuffer *buffer);

/* The cell at (x, y), or nullptr outside the grid. Valid until the grid
 * is next written, adopted, or released. */
const OvdCell *ovd_buffer_cell(const OvdBuffer *buffer, int32_t x, int32_t y);

/* Writes grapheme at (x, y) in style, both halves if it is wide. Before
 * each cell is overwritten, the other half of any wide grapheme it
 * belongs to becomes a space in its own style (R20), even if the caller
 * clips it. Returns false and changes nothing if (x, y) is outside the
 * grid, the grapheme is wide and x is the last column, or grapheme is
 * nullptr or breaks OvdGrapheme's length invariant. style is stored as
 * given. */
bool ovd_buffer_put(OvdBuffer *buffer, int32_t x, int32_t y, const OvdGrapheme *grapheme,
                    OvdStyle style);

/* Writes a space at (x, y) in style, with the same repair as
 * ovd_buffer_put. Returns false and changes nothing outside the grid. */
bool ovd_buffer_put_space(OvdBuffer *buffer, int32_t x, int32_t y, OvdStyle style);

/* The canvas (render.h): a view of one frame buffer plus a clip.
 *
 * Open exactly when frame is non-null; a zero-initialized OvdCanvas is
 * closed. While open, clip lies within the frame's bounds, and an empty
 * clip is {0, 0, 0, 0} (R18). */
struct OvdCanvas {
    OvdBuffer *frame;
    OvdRect clip;
};

/* Opens canvas on frame for a new frame: clears frame and sets the clip
 * to its whole area (the canvas half of ovd_frame_begin). */
void ovd_canvas_open(OvdCanvas *canvas, OvdBuffer *frame);

/* Closes canvas (frame end, abandon, and while the sink runs; R23, R24).
 * The frame buffer is left as drawn. */
void ovd_canvas_close(OvdCanvas *canvas);

#endif /* OVERDRIVE_SRC_BUFFER_H */
