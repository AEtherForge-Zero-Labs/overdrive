## Context

`include/overdrive/render.h` fixes the public canvas contract;
`research.md` R18 to R23 fix clipping, wide-glyph, invalid-argument, and
closed-canvas behavior; R15 to R17 are implemented by
`ovd_unicode_next_grapheme` (OVD-03); R31 and R39 fix how storage is
allocated (`ovd_alloc_all`, then each owner adopts its block). The
terminal that opens and closes frames is OVD-10 and the renderer that
reads the grid is OVD-08. This change gives both of them the narrowest
private interface they need and nothing more.

## Goals / Non-Goals

**Goals:** a cell grid that can never hold half a wide glyph, whatever
sequence of writes reaches it; the six public canvas functions, exact to
their comments for any `int32_t` arguments; closed and nullptr canvases
that do nothing; no allocation in any drawing call.

**Non-Goals:** frames (`ovd_frame_begin`/`_end`/`_abandon`, OVD-10),
diffing or copying grids (OVD-08), boxes (`ovd_canvas_box`, OVD-09),
style downsampling (OVD-02).

## Decisions

- **`OvdCell` is a plain value in the private header:**
  - `text[16]`, NUL-terminated, at most 15 bytes;
  - `length`;
  - `width`, which is 0, 1, or 2;
  - `style`.

  The renderer reads it once per cell, so an accessor call per field would
  be waste. It has no invariant beyond its fields. The pairing invariant
  belongs to the grid (Art. IV).
- **`OvdBuffer { OvdBlock storage; OvdSize size; }` is an owning module.**
  The struct is in `buffer.h` only so the terminal can embed it. Only
  `buffer.c` changes its fields. Zero-initialized means empty, with size
  `{0, 0}`.
  - Storage follows R31 without a callback interface (R39). The caller
    gets the request from `ovd_buffer_request(size, &request)` and
    batches it with the terminal's other requests in `ovd_alloc_all`.
    Then `ovd_buffer_adopt` releases the old block, takes the new one,
    and clears it.
  - `ovd_buffer_request` fails as follows:
    - `OVD_ERROR_INVALID_ARGUMENT` for a non-positive dimension (R22).
    - `OVD_ERROR_ALLOCATION` when `cols × rows` overflows `size_t`. It
      cannot overflow on a 64-bit target, but can on a 32-bit one (R32).
  - `out` must hold no live blocks when it is passed to `ovd_alloc_all`,
    because a failure empties it.
- **Pairing lives in the grid's writes, not in the canvas.**
  `ovd_buffer_put` takes `(x, y, grapheme, style)` and
  `ovd_buffer_put_space` takes `(x, y, style)`.
  - Before a cell is overwritten, its partner half becomes a space in its
    own style. For a head (width 2) the partner is the next cell, and for
    a tail (width 0) it is the previous cell. That one rule is R20.
  - The canvas never writes outside the clip itself, so R20's repair is
    the only write outside the clip, as FR-030 says.
  - A wide grapheme whose second column is off the grid is refused: the
    grid stays unchanged and the function returns false. Out-of-bounds
    coordinates are refused the same way.
  - This makes the invariant hold for any caller, not only for a correct
    canvas.
- **The tail cell carries the head's style.** So "the other half keeps
  its own style" (R20) means the same thing from either side.
- **The canvas is a view of one buffer:**
  `struct OvdCanvas { OvdBuffer *frame; OvdRect clip; }`.
  - A canvas is open exactly when `frame` is non-null.
  - Zero-initialized means closed, so no init function is needed
    (Art. VII).
  - `ovd_canvas_open(canvas, frame)` clears the buffer to spaces in
    `OVD_STYLE_DEFAULT` and sets the clip to the whole buffer: that is
    the canvas half of `ovd_frame_begin`.
  - `ovd_canvas_close` sets `frame` to nullptr: that is the canvas half of
    frame end, abandon, and the sink (R23, R24).
  - The struct is declared in `buffer.h`, because the issue owns no
    `canvas.h` and adding one would be a new header with no present need.
  - Only `canvas.c` changes its fields.
- **Geometry is done in `int64_t` (R18).** A rect spans `[x, x + cols)`.
  A non-positive extent is empty. An empty intersection is
  `{0, 0, 0, 0}`.
- **`ovd_canvas_text` keeps a 64-bit column and a 64-bit total.**
  - It stops moving the column once it is past `INT32_MAX` (the grid ends
    long before then).
  - It keeps segmenting to the end of the text, because the return value
    counts clipped graphemes too. The total saturates at `INT32_MAX`.
  - A wide grapheme that has both columns inside the clip is written
    whole. Otherwise each column that is inside gets a space in the
    call's style (R19).
- **Styles are normalized at the canvas boundary (R22).**
  - A color keeps only its kind's fields. An unknown kind becomes DEFAULT.
  - Attributes are masked to the six defined bits. The mask is a
    `static constexpr` in `canvas.c` (Art. III).
  - The grid stores what it is given. Normalizing once at the boundary
    keeps `ovd_buffer_put` a plain store.
- **`ovd_canvas_cell` with a nullptr `out` returns false.** It is
  infallible, and R22 says an infallible function given nullptr does
  nothing and returns a zero value.
- **Tests use the internal functions directly.** They build a buffer
  through `ovd_buffer_request`, `ovd_alloc_all` (with the counting test
  allocator), and `ovd_buffer_adopt`, then open a canvas on it. There is
  no terminal yet. Shared builds hide the internal symbols, so the test
  compiles `buffer.c`, `canvas.c`, `unicode.c`, and `alloc.c` in, as
  `test_unicode` and `test_alloc` do.

## Risks / Trade-offs

- `buffer.h` becomes OVD-08's and OVD-10's private contract. The
  trade-off: if they need a function it lacks (for example a grid copy),
  that is a change to a file this issue owns, made by their issue after
  this one merges.
- A cell is about 30 bytes, so a 500×200 terminal costs roughly 3 MB for
  two grids. That is acceptable for now. There is no measured complaint.

## Assumptions (R42, labeled `assumption`)

The following cases are not covered by render.h or research:

- `ovd_canvas_cell(canvas, x, y, nullptr)` returns false.
- A wide grapheme that `ovd_buffer_put` refuses leaves the grid unchanged.
- Opening a canvas clears its buffer.
- The canvas struct lives in `buffer.h`.

For each, the decision above takes the narrowest reading.
