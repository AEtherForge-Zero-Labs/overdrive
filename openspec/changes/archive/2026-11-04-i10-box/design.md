## Context

`include/overdrive/render.h` fixes the contract:
- an area narrower or shorter than 2 cells draws nothing;
- an out-of-range border draws nothing;
- the interior is left as it was;
- drawing never fails or allocates for any `int32_t` arguments.

R18 fixes overflow-safe geometry. R19 and R20 fix wide glyphs and the
clip. R22 fixes normalization, and R23 fixes closed canvases. OVD-07
already implements all of these behind the public canvas functions.

## Goals / Non-Goals

**Goals:** the five border styles exactly as render.h's comments show
them; correct behavior for any `int32_t` area, at a cost bounded by the
canvas size, not the area size.

**Non-Goals:** titles, joined borders (T-junctions), shadows, or filled
interiors. No present requirement asks for them (Art. VII).

## Decisions

- **Only the public path.** Every border cell is one
  `ovd_canvas_text(canvas, x, y, glyph, length, style)` call. The clip
  (FR-030), the R20 repair, R22 normalization, and closed or nullptr
  canvases (R23) therefore behave exactly as they do for text. `box.c`
  never touches `struct OvdCanvas` fields (Art. IV).
- **Glyphs.**

  | Border | Corners (TL, TR, BL, BR) | Horizontal | Vertical |
  | --- | --- | --- | --- |
  | light | U+250C U+2510 U+2514 U+2518 | U+2500 | U+2502 |
  | heavy | U+250F U+2513 U+2517 U+251B | U+2501 | U+2503 |
  | double | U+2554 U+2557 U+255A U+255D | U+2550 | U+2551 |
  | rounded | U+256D U+256E U+2570 U+256F | U+2500 | U+2502 |
  | ascii | `+ + + +` | `-` | `\|` |

  They are stored as one table indexed by `OvdBorder`. Every glyph is
  East Asian Width A or Na, so each takes one column (R5).
- **Geometry in 64 bits (R18).** The box spans `x0 = area.x` to
  `x1 = area.x + area.width - 1`, and `y0` to `y1` the same way, all
  computed in `int64_t`. Each edge is drawn only over its intersection
  with `[0, cols) × [0, rows)` of `ovd_canvas_size`. Every coordinate
  passed to `ovd_canvas_text` therefore fits in `int32_t`. A loop runs at
  most `cols` or `rows` times, whatever the area.
- **The size test uses the area itself.** `width < 2 || height < 2`
  draws nothing, wherever the area lies. A 10×4 box that the clip cuts
  still draws its visible part (US3 scenario 2).
- **Degenerate boxes.** In a 2×2 box only the corners are drawn. A box 2
  wide has no horizontal edge cells, and one 2 tall has no vertical edge
  cells.
- **Edge order.** The top row is drawn left to right, then the bottom
  row, then the left and right columns between them. Edges never
  overlap, so the order is not observable.

## Risks / Trade-offs

- **One call per cell.** Each border cell costs one `ovd_canvas_text`
  call, which segments one grapheme. A dedicated buffer path would be
  faster. The canvas size bounds the cost, and no benchmark asks for
  more (Art. VII).
