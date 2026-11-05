## 1. Sanitizer preset first

- [x] 1.1 Add the `sanitize` configure, build and test presets (T104, design D2) and run the existing suite under them on Apple Clang. Any report in existing code becomes a separate bug issue, and this change halts.

## 2. Contract freeze and config, tests first

- [x] 2.1 Copy the three contract headers into `include/overdrive/` and add `input.h` to `overdrive_public_headers` (T101).
- [x] 2.2 Write the config tests in `tests/test_terminal.c`: defaults, R115 rejections, and the interim `OVD_ERROR_UNSUPPORTED` on both backends (T102). Watch them fail.
- [x] 2.3 Implement the defaults and validation in `src/terminal.c` (T103, design D1), then watch the tests pass.

## 3. CI and baseline

- [x] 3.1 Add the Linux GCC 14 and macOS Apple Clang sanitize rows to `.github/workflows/ci.yml` (T104).
- [x] 3.2 Add the Sanitizers section to `.specify/memory/baseline.md` (design D3).

## 4. Delivery evidence

- [x] 4.1 Run `debug`, `sanitize` and `windows-cross-debug` (configure, build, test) from clean builds with zero warnings, plus `shared` and `debug-clang` as a falsification pass.
- [x] 4.2 Tick T101–T104 in `specs/002-input-restore/tasks.md` and reread the full diff.
