## 1. Tests first

- [x] 1.1 Extend `tests/test_system_pty.c` for T114 (design D7), including create's refusals with input on.
- [x] 1.2 Build against the interim error and watch the new cases fail.

## 2. Implementation

- [x] 2.1 Platform layer: raw-input step, POSIX resize wakeup, wait, read, clock, with citations (T115, design D1, D2, D4).
- [x] 2.2 `src/terminal_system.c`, `src/internal.h`: stdin check, report and CAN sequences, platform wrappers (T116, design D5).
- [x] 2.3 `src/input.c`: the system wait loop (design D3). `src/terminal.c`: remove the interim error and pass the input options; drop `tests/test_terminal.c`'s interim assertion (design D6).
- [x] 2.4 Watch `test_system_pty`, `test_terminal` and the full suite pass.

## 3. Delivery evidence

- [x] 3.1 Run `debug`, `sanitize`, `windows-cross-debug` and gcc-14 `release` from clean with zero warnings, plus `shared` and `debug-clang`.
- [x] 3.2 Check plan.md's terminal-mode table against the code, add research.md R120, tick T114–T116, and reread the full diff.
