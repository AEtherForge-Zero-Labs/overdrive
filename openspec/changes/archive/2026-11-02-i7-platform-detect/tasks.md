## 1. Plan

- [x] 1.1 Record the change and post the plan comment on #7.

## 2. Detection

- [x] 2.1 Write `tests/test_detect.c`, register it, and observe it fail (T023).
- [x] 2.2 Implement `src/detect.h` and `src/detect.c` (T023).

## 3. Platform

- [x] 3.1 Write `src/platform/platform.h` (T020).
- [x] 3.2 Implement `src/platform/platform_posix.c` and check enter, leave, size, and write under a pseudo-terminal with a scratch harness (T021).
- [x] 3.3 Implement `src/platform/platform_windows.c` and build it with the `windows-cross-debug` preset (T022).
- [x] 3.4 Record R40 in `research.md`; tick T020–T023.

## 4. Delivery evidence

- [x] 4.1 Run the debug, debug-clang, shared, and windows-cross-debug presets with zero warnings.
- [x] 4.2 Reread the full diff; run the OpenSpec and docs-form checks.
