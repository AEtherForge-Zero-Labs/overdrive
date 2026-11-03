/*
 * Differential renderer (private): the bytes that turn the screen the
 * terminal is known to show into the next frame. FR-020, FR-021, FR-024,
 * FR-025; research.md R8, R30, R33, R34, R38, R43.
 *
 * Transformational: (screen, frame) -> bytes appended to the caller's
 * output. Nothing outlives a call and nothing allocates. Holding the two
 * grids, swapping them after a frame, and marking the screen unknown
 * after a failed write belong to the terminal.
 */
#ifndef OVERDRIVE_SRC_DIFF_H
#define OVERDRIVE_SRC_DIFF_H

#include "buffer.h"
#include "output.h"

#include <overdrive/core.h>
#include <overdrive/terminal.h>

#include <stddef.h>

/* Appends to output the bytes that change screen into frame at depth.
 *
 * screen is the grid the terminal is known to show, or nullptr when that
 * is unknown (FR-025). In that case the output is a full redraw: CSI 0 m
 * CSI 2 J, then every cell that is not a default space.
 *
 * A non-empty output is framed as R34 says, with CSI ? 2026 h/l around it
 * when synchronized is set. Glyphs are written only for cells that
 * differ. Cells are compared field by field: text, width, and style as
 * stored, not as depth shows them. A wide glyph is written when either of
 * its halves differs. A CUP precedes a glyph unless the cursor is known
 * to be at its cell. The cursor is unknown at the start of the output,
 * after a glyph reaches the last column (R33), and after a cell whose
 * text ends in ZWJ (R38). The output never relies on autowrap. If frame
 * equals screen, nothing is appended (FR-021).
 *
 * Relies on buffer.h's invariants for both grids. The terminal's SGR
 * state must be the default when the output starts. Every non-empty
 * output ends with CSI 0 m, so it then leaves the next output's state
 * default too.
 *
 * Errors:
 * - OVD_ERROR_INVALID_ARGUMENT, with nothing appended, when frame or
 *   output is nullptr, frame is empty, screen's size differs from
 *   frame's, or depth is out of range.
 * - OVD_ERROR_IO when an append does not fit. That cannot happen while
 *   the output has room for ovd_diff_bound's bytes beyond its current
 *   length (R30). The output is then reset to empty. */
[[nodiscard]] OvdResult ovd_diff_render(const OvdBuffer *screen, const OvdBuffer *frame,
                                        OvdColorDepth depth, bool synchronized,
                                        OvdOutput *output);

/* Stores in *bytes the most ovd_diff_render can append for a grid of
 * size (R30). That is:
 * - 16 when synchronized, plus 8 for the full-redraw prefix and 4 for the
 *   trailing reset;
 * - for each cell, the longest CUP at size, plus OVD_COLOR_SGR_MAX_LENGTH,
 *   plus OVD_GRAPHEME_MAX_BYTES.
 *
 * Errors, each leaving *bytes unchanged:
 * - OVD_ERROR_INVALID_ARGUMENT when bytes is nullptr or a dimension is
 *   not positive;
 * - OVD_ERROR_ALLOCATION when the bound overflows size_t. */
[[nodiscard]] OvdResult ovd_diff_bound(OvdSize size, bool synchronized, size_t *bytes);

#endif /* OVERDRIVE_SRC_DIFF_H */
