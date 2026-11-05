## 1. The test

- [x] 1.1 Write `tests/test_system_pty.c`: the pty fixture with descriptor save, check, and restore; the sentinel reader; the termios comparison; and the headless twin with a capturing sink.
- [x] 1.2 Add the cases: enter and exit bytes with and without `alternate_screen`; allocation refusals 1 to 4 and all; zero sizes at create; a read-only stdout (step 7); the frame sequence through two resizes, a 0×0 begin, and a mid-frame resize; and 1×1.
- [x] 1.3 Register the test in `tests/CMakeLists.txt` under `if(UNIX)`, append-only, and watch it pass under `ctest --preset debug`.

## 2. Non-vacuity

- [x] 2.1 Run the test against scratch mutations of `src/`, one at a time, and watch each fail: drop the `undo_open` call after a refused allocation; drop the `undo_open` call after a failed enter write; drop `free_terminal` after a failed enter write; skip the per-frame size read in `ovd_frame_begin`; leave `last_rows` at the create-time value; drop `ALTERNATE_LEAVE` from the exit sequence; drop the mode restore after a size that reads 0. Revert each, and confirm `git status` shows no change under `src/`.

## 3. Delivery evidence

- [x] 3.1 Build and test the `debug`, `release`, `debug-clang`, and `shared` presets from clean builds with zero warnings, and configure and build `windows-cross-debug` if MinGW is installed (the test must not be built there).
- [x] 3.2 Correct `tests/test_terminal.c`'s comment that a valid system config is tested only with stdout redirected (found by the close's vocabulary scan); comment only.
- [x] 3.3 Reread the full diff and post the plan comment on #43, linking this change; the PR and its evidence are published by `tx:close`.
