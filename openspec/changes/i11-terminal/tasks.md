## 1. Tests first

- [x] 1.1 Write `tests/test_terminal.c` (T031) and register it, then watch it fail before the implementation exists.

## 2. Implementation

- [x] 2.1 Implement `src/internal.h` and `src/terminal.c`: the headless backend, frame begin/end/abandon, one write per frame, and optional sync markers (T032).

## 3. Delivery evidence

- [x] 3.1 Run all six presets with zero warnings; `ctest --preset debug -R test_terminal` passes.
- [x] 3.2 Record R44 in `research.md`, tick T031 and T032, and note on #12 what OVD-11 must change in `terminal.c` and `internal.h`.
- [ ] 3.3 Reread the full diff and publish the PR with evidence.
