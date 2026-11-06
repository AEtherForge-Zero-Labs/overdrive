## 1. Tests first

- [x] 1.1 Write `tests/consumer/main.c` and `tests/consumer/CMakeLists.txt` (T201).
- [x] 1.2 Write `tests/consumer/run_consumer.cmake` (T202).
- [x] 1.3 Register `consumer_vendored` and `consumer_duplicate` with `TIMEOUT 300`, and watch both fail on the unchanged `CMakeLists.txt` (T203).

## 2. Build changes

- [x] 2.1 R201 test default, R202 output directory and in-source check, R203 guard with both messages (T204).
- [x] 2.2 R204 public header file set; header checks pass unchanged (T205).
- [x] 2.3 Found while implementing: give the version outside `project()` (new decision R213), and use `copy -t` for the Windows DLL step (R209). Record both in `specs/003-packaging/research.md` and the contract's snippet.

## 3. Proof

- [x] 3.1 Every T206 preset from clean, with 0 warnings and all tests passing (T206).
- [x] 3.2 Check the ASSUMED items R202 (directory scope), R203 (global property), and R209 (empty copy) against the CMake documentation.
