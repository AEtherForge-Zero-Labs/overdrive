## 1. Tests first

- [x] 1.1 Add the counting and failing test allocator in `tests/support/` (T016).
- [x] 1.2 Write `tests/test_alloc.c` and watch it fail before the implementation exists (T016).

## 2. Implementation

- [x] 2.1 Implement allocator dispatch with overflow-checked sizes in `src/alloc.{c,h}` (T017).
- [x] 2.2 Implement the fixed-capacity output buffer in `src/output.{c,h}` (T017).

## 3. Delivery evidence

- [x] 3.1 Run the debug, debug-clang, and shared presets with zero warnings.
- [x] 3.2 Record the R30/R31 reading in `research.md`; tick T016/T017.
- [x] 3.3 Reread the full diff and publish the PR with evidence.

## 4. Review fixes

- [x] 4.1 `ovd_alloc_all` empties `out` before its argument checks, as `src/alloc.h` promises (found in review). `tests/test_alloc.c` seeds non-empty slots on every batch failure and checks that a slot past `count` is untouched; watched it fail first.
- [x] 4.2 Record the fix in R39.
