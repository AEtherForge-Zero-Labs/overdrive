## 1. Tests first

- [x] 1.1 Write `tests/test_input.c` (T110, T111, and T112's create-failure part) and register it.
- [x] 1.2 Extend `tests/test_steady_state.c` with the input case (T112).
- [x] 1.3 Build against the interim error and watch both fail.

## 2. Implementation

- [x] 2.1 Add the input state to `src/internal.h` and the allocation and release to `src/terminal.c`. Keep the interim error for the system backend only, and narrow `tests/test_terminal.c`'s interim assertion to match (design D4).
- [x] 2.2 Implement `src/input.c` (T113, design D1–D3, D5) and append it to `CMakeLists.txt`.
- [x] 2.3 Watch `test_input`, `test_steady_state` and `test_terminal` pass.

## 3. Delivery evidence

- [x] 3.1 Run `debug`, `sanitize`, `windows-cross-debug` and gcc-14 `release` from clean with zero warnings, plus `shared` and `debug-clang`.
- [x] 3.2 Tick T110–T113 in `specs/002-input-restore/tasks.md` and reread the full diff.
