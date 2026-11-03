## Context

The inputs are fixed elsewhere:
- `src/buffer.h` (OVD-07) fixes the grid. Cells are plain values, and a
  width-2 cell is always followed by a width-0 cell.
- `src/output.h` (OVD-04) is a fixed-capacity byte buffer. Its appends
  are all or nothing.
- `src/color.h` (OVD-02) gives `ovd_color_sgr_encode`, which normalizes
  and downsamples both styles. Its result is at most 48 bytes.

R8 fixes the strategy, R30 the bound, R33 the wrap rule, and R34 the
framing. R38 fixes the oracle's rules. One of them binds the diff: after
a cell whose text ends in ZWJ, the next glyph needs a CUP.

## Goals / Non-Goals

**Goals:**
- Output that replays on the strict oracle to exactly the frame
  (SC-001).
- 0 bytes for an unchanged frame (SC-002).
- At most 64 bytes for a one-cell change up to 999×999 (SC-003).
- A bound that no frame can exceed (R30).

**Non-Goals:**
- Holding the screen and frame buffers, swapping them, or marking the
  screen unknown. These are OVD-10's job.
- Writing to a sink (OVD-10).
- Sending a run of unchanged cells instead of a CUP. R8 permits it but
  does not require it, and no budget needs it.

## Decisions

- **Transformational (plan.md's module table).** One function maps
  `(screen | nullptr, frame, depth, synchronized)` to bytes appended to
  the caller's output. Nothing outlives the call. `screen == nullptr`
  means the screen is unknown (FR-025), and the output is a full redraw.
- **Equality is fieldwise:** length, the text bytes up to length, width,
  and each style field. It never uses `memcmp`, because `OvdStyle` has a
  padding byte. Width is part of equality, so any change that splits a
  wide glyph on screen also changes the cell holding its other half.
  Both halves are therefore rewritten, and the oracle never keeps an
  indeterminate cell.
- **Leads only.** The loop compares and writes lead cells (width 1 or
  2) only. A continuation never needs its own comparison: it is width 0
  and empty, in its lead's style (buffer.h), so it equals the screen's
  whenever its lead does. A mutant that ignored width survived until a
  direct test (`width_compared`) covered it.
- **Full redraw** compares each cell against a default space (text " ",
  width 1, `OVD_STYLE_DEFAULT`), which is what `CSI 2 J` leaves. So it
  writes only non-default cells (R34).
- **Cursor.** The cursor is tracked as known-at-(x, y) or unknown. It is
  unknown at the start of every frame, and again after a cell whose text
  ends in ZWJ (R38). After a glyph, the tracked column advances by its
  width. After the last column it is `cols`, which matches no cell, so
  R33's pending wrap needs no extra state. A CUP is emitted whenever the
  cursor is not known to be at the next cell.
- **Style.** The terminal's style is tracked as the last style sent. It
  starts as default, because every non-empty frame ends with `CSI 0 m`
  and a full redraw starts with it. Each cell's SGR is
  `ovd_color_sgr_encode(current, cell.style, depth)`.
- **Framing (R34).** In order:
  - `CSI ? 2026 h`, when synchronized;
  - `CSI 0 m CSI 2 J`, for a full redraw;
  - the updates;
  - `CSI 0 m`;
  - `CSI ? 2026 l`, when synchronized.

  When nothing differs on a known screen, the output gets nothing.
- **Errors.** `OVD_ERROR_INVALID_ARGUMENT` is returned, and nothing is
  appended, in these cases:
  - `frame` or `output` is nullptr;
  - the frame is empty;
  - the screen's size differs from the frame's;
  - the depth is out of range.

  `OVD_ERROR_IO` is returned when an append does not fit. That breaks
  R30 and is a bug. The output is then reset to empty, and the terminal
  treats the frame as failed.
- **Bound (R30).** The bound is fixed overhead plus a per-cell term:
  - fixed overhead: 16 bytes when synchronized, the full-redraw prefix
    of 8, and the trailing reset of 4;
  - per cell: a CUP of `4 + digits(rows) + digits(cols)`,
    `OVD_COLOR_SGR_MAX_LENGTH`, and `OVD_GRAPHEME_MAX_BYTES`.

  Every emitted lead writes at most one CUP, one SGR, and its text, and
  there are at most `cols × rows` leads. The products and sums use
  `ckd_*`. On overflow the result is `OVD_ERROR_ALLOCATION`, matching
  `ovd_buffer_request`.

## Risks / Trade-offs

- **The bound is large.** It is about 71 bytes a cell at 80×24, about
  136 KB in all. R30 accepts a proven bound over a tight one.
- **One CUP per run.** Each run of changed cells pays a CUP, even when
  the gap is one cell. This is within SC-003, and R8 allows it.
- **Cells are compared raw, not after downsampling.** A change the
  depth cannot show still rewrites the cell. That is correct, but costs
  a few bytes.
