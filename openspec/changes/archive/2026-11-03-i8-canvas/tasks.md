## 1. Tests first

- [x] 1.1 Write `tests/test_canvas.c` (T024) and register it, then watch it fail before the implementation exists.

## 2. Implementation

- [x] 2.1 Implement `src/buffer.h`/`src/buffer.c` with the `OvdBuffer` grid, storage through an allocator batch, and the wide-glyph pairing invariant (T025).
- [x] 2.2 Implement `src/canvas.c`: `ovd_canvas_size`, `_clip`, `_set_clip`, `_fill`, `_text`, `_cell` (T026).

## 3. Delivery evidence

- [x] 3.1 Run the debug, debug-clang, and shared presets with zero warnings; `ctest --preset debug -R test_canvas` passes.
- [x] 3.2 Record R42 in `research.md` and tick T024 to T026.
- [x] 3.3 Reread the full diff and publish the PR with evidence.

## 4. Review fixes

- [x] 4.1 `tests/CMakeLists.txt`: `test_canvas` is compiled with `OVERDRIVE_BUILDING`, so `canvas.c`'s `OVD_API` definitions are not `dllimport` in a Windows shared build (found in review and by CI's MinGW job). Seen failing on `windows-cross-shared`, then all six presets green.
