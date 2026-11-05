## 1. Tests first

- [x] 1.1 Write `tests/test_decode.c` with the SC-001 key table in readable notation, every row split at every byte boundary and fed one byte per call (T105), and register it in `tests/CMakeLists.txt`.
- [x] 1.2 Add the SGR mouse rows (T106) and the paste and focus rows (T107).
- [x] 1.3 Build against a stub `src/decode.c` that consumes every byte and returns no event, and watch every table group fail.

## 2. Implementation

- [x] 2.1 Add `ovd_unicode_decode_utf8` to `src/unicode.{h,c}` (design D1) and run `test_unicode` and `test_canvas` unchanged.
- [x] 2.2 Implement `src/decode.{h,c}` (T108, design D2–D5) and append `src/decode.c` to the library's sources in `CMakeLists.txt`.
- [x] 2.3 Watch `test_decode` pass.

## 3. Fuzz

- [x] 3.1 Write `tests/test_decode_fuzz.c` (T109): at least 10⁷ seeded bytes, invariants after every call, a known sequence decoding after each recovery. Register it, and watch it fail against a deliberately broken invariant before passing.

## 4. Delivery evidence

- [x] 4.1 Run `debug`, `sanitize` and `windows-cross-debug` from clean builds with zero warnings, plus `shared` and `debug-clang` as a falsification pass.
- [x] 4.2 Tick T105–T109 in `specs/002-input-restore/tasks.md` and reread the full diff.
