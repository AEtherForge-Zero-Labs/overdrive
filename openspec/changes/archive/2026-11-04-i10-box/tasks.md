## 1. Tests first

- [x] 1.1 Write `tests/test_box.c` (T029) and register it, then watch it fail before the implementation exists. Seen failing against a stub that draws nothing: 7 of 8 groups (`draws_nothing` passes, as it should). Five of six mutants of `src/box.c` were then killed. The sixth, dropping `put`'s on-canvas check, is equivalent: an off-canvas far edge converts to a negative `int32_t`, which the canvas already discards. The check stays so that no out-of-range value is converted.

## 2. Implementation

- [x] 2.1 Implement `src/box.c`: `ovd_canvas_box` (T030).

## 3. Delivery evidence

- [x] 3.1 Run every preset, plus CC=gcc-14 and LLVM Clang 22, with zero warnings; `ctest --preset debug -R test_box` passes.
- [x] 3.2 Record R45 in `research.md` and tick T029 and T030.
- [x] 3.3 Reread the full diff and publish the PR with evidence.
