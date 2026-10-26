/*
 * Color downsampling and SGR encoding (FR-023; research R7, R8, R9, R37).
 *
 * Internal to Overdrive: nothing here is exported. Transformational: every
 * function is pure, keeps no state, and never allocates.
 */
#ifndef OVERDRIVE_SRC_COLOR_H
#define OVERDRIVE_SRC_COLOR_H

#include <overdrive/render.h>

#include <stddef.h>

/* The longest sequence ovd_color_sgr_encode writes, 48 bytes. Two shapes
 * reach it: a delta adding all six attributes and two RGB colors
 * (ESC [ 1;2;3;4;7;9;38;2;255;255;255;48;2;255;255;255 m), and a reset
 * with five attributes and two RGB colors (a reset means one attribute
 * was removed, so at most five remain). A macro rather than a constexpr
 * object because the baseline's Clang 18 lacks C23 object constexpr. */
#define OVD_COLOR_SGR_MAX_LENGTH ((size_t)48)

/* Returns color as depth can show it, with only its kind's fields set
 * (R9, R37):
 * - NONE (and any out-of-range depth): OVD_COLOR_DEFAULT.
 * - DEFAULT, and an out-of-range kind: OVD_COLOR_DEFAULT.
 * - Indexed 0-15: unchanged at 16, 256, and truecolor (they follow the
 *   user's theme).
 * - Indexed 16-255: unchanged at 256 and truecolor; at 16, the nearest
 *   xterm default 16 color to the index's xterm RGB value.
 * - RGB: unchanged at truecolor; at 256, the nearest of indices 16-255
 *   (6x6x6 cube and 24-step gray ramp); at 16, the nearest xterm default
 *   16 color.
 * Nearest is by squared distance in sRGB; ties go to the lowest index. */
[[nodiscard]] OvdColor ovd_color_downsample(OvdColor color, OvdColorDepth depth);

/* Writes to buffer the SGR sequence that changes the terminal's style
 * from `from` to `to` at depth, and stores its length in *length.
 *
 * Both styles are normalized (undefined attribute bits dropped, colors
 * downsampled for depth) before they are compared. If they are then
 * equal, nothing is written and *length is 0. If `to` keeps every
 * attribute of `from`, the sequence is a delta: the added attributes, then
 * the foreground if it changed, then the background if it changed (39 and
 * 49 return to the default). Otherwise it starts with parameter 0 (reset)
 * followed by every attribute and every non-default color of `to`.
 * Attributes are written in ascending code order: 1, 2, 3, 4, 7, 9.
 * Only R7's SGR subset is emitted; the result is at most
 * OVD_COLOR_SGR_MAX_LENGTH bytes and is not NUL-terminated.
 *
 * Returns false, writing nothing to buffer or *length, if length is
 * nullptr, or if the sequence is non-empty and buffer is nullptr or
 * capacity is smaller than the sequence. */
[[nodiscard]] bool ovd_color_sgr_encode(OvdStyle from, OvdStyle to, OvdColorDepth depth,
                                        char *buffer, size_t capacity, size_t *length);

#endif /* OVERDRIVE_SRC_COLOR_H */
