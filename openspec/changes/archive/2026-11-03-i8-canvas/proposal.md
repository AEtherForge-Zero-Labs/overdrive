## Why

Issue #8 (OVD-07, tasks T024 to T026) provides the cell grid and the
drawing surface that every frame is described on (FR-030 to FR-033). The
differential renderer, the box drawer, and the terminal (OVD-08, OVD-09,
OVD-10) are blocked on it.

## What Changes

- Add `src/buffer.h`/`src/buffer.c`: `OvdCell`, `OvdBuffer` (a grid of
  cells whose storage comes from an allocator batch, R31), and the cell
  writes that keep the wide-glyph pairing invariant (FR-030, R20). The
  private header also declares `struct OvdCanvas` and its open and close
  functions, so the terminal can embed the canvas once (R23).
- Add `src/canvas.c`: the public `ovd_canvas_size`, `_clip`,
  `_set_clip`, `_fill`, `_text`, and `_cell` (render.h), with R18
  overflow-safe geometry, R19 cut wide glyphs, R22 normalization, and
  R23 closed-canvas behavior.
- Add `tests/test_canvas.c`, registered with CTest.
- Append the two sources to the library target and the test to
  `tests/CMakeLists.txt`.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements remain in Spec Kit; this transaction
implements T024 to T026 without changing their requirements.

### Modified Capabilities

None.

## Impact

Only the paths issue #8 owns, append-only lines in the two CMake files,
the T024 to T026 ticks in `specs/001-render-core/tasks.md`, and one
decision entry (R42) in `specs/001-render-core/research.md`. No public
header changes.
