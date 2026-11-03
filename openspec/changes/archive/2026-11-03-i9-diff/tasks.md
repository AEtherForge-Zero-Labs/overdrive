## 1. Tests first

- [x] 1.1 Write `tests/test_diff.c` (T027) and register it, then watch it fail before the implementation exists. Seen failing at link time (no `ovd_diff_render`/`ovd_diff_bound`). Nine mutants of `src/diff.c` were then each killed; the width mutant only after `width_compared` was added.

## 2. Implementation

- [x] 2.1 Implement `src/diff.h`/`src/diff.c`: `ovd_diff_render` and `ovd_diff_bound` (T028).

## 3. Delivery evidence

- [x] 3.1 Run every preset, plus CC=gcc-14 and LLVM Clang 22, with zero warnings; `ctest --preset debug -R test_diff` passes.
- [x] 3.2 Record R43 in `research.md` and tick T027 and T028.
- [x] 3.3 Reread the full diff and publish the PR with evidence.
