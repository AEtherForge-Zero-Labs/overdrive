## 1. Tests first

- [x] 1.1 Write `tests/test_system_notty.c` and its CMake driver (T034), register it, and drop the R44 UNSUPPORTED check from `tests/test_terminal.c`; watch `test_system_notty` fail against the current `OVD_ERROR_UNSUPPORTED` branch.

## 2. Implementation

- [x] 2.1 Add `OvdTerminalSystem` and the `ovd_terminal_system_*` declarations to `src/internal.h`, and implement them in `src/terminal_system.c` (T033).
- [x] 2.2 Wire `src/terminal.c`: R28 order in create with rollback, destroy's exit and restore, the size re-read in begin, and the write in end; append the source to `CMakeLists.txt`.

## 3. Delivery evidence

- [x] 3.1 Run all six presets from clean builds with zero warnings; `ctest --preset debug -R test_system_notty` and `ctest --preset windows-cross-debug` pass.
- [x] 3.2 In a scratch pty run on macOS, observe the enter and exit bytes, echo restore, a 1×1 and a 0×0 size, and a resize between frames.
- [x] 3.3 Record R46 in `research.md`, and tick T033 and T034 in `tasks.md`.
- [x] 3.4 Reread the full diff and post the plan comment on #12; the PR and its evidence are published by `tx:close`.
