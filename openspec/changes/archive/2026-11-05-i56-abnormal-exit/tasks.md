## 1. Tests first

- [x] 1.1 Write `tests/test_restore_pty.c` (T117, design D6) and register it in `tests/CMakeLists.txt` with the sanitizer options.
- [x] 1.2 Build against the current code and watch the restoration cases fail. The key and `restore_on_exit` false cases may already pass; record which. Recorded: before the implementation every signal case, the real fault, the partial frame, the second terminal, and the application crash handler failed, as did loop seeds 4, 5, and 7; the keys, both SIGTERM handler cases, SIGHUP ignored, `restore_on_exit` false, and crash after destroy passed.

## 2. Implementation

- [x] 2.1 `src/platform/platform.h`, `src/platform/platform_posix.c`: the record, the state machine, the handler, and the mask (T118, design D1–D3), with POSIX.1-2024 citations.
- [x] 2.2 `src/platform/platform_windows.c`: the console handler with option B, the exception filter, and `SIGABRT` (T119, design D4), with Microsoft and UCRT citations.
- [x] 2.3 `src/terminal_system.c`, `src/internal.h`: reserve, arm, release, claim, and disarm (T120, design D5).
- [x] 2.4 Watch `test_restore_pty` and the full suite pass.

## 3. Delivery evidence

- [x] 3.1 Run `debug`, `sanitize`, `windows-cross-debug`, and gcc-14 `release` from clean with zero warnings, plus `shared` and `debug-clang`.
- [x] 3.2 Update the docs (design D7), tick T117–T120, and reread the full diff.
