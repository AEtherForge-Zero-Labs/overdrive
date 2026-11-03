## 1. Tests first

- [x] 1.1 Write `tests/test_vt_replay.c` and the CMake lines; watch it fail

## 2. Oracle

- [x] 2.1 Implement `tests/support/screen.{c,h}` (model, rows, compare, dump)
- [x] 2.2 Implement `tests/support/vt_replay.{c,h}` (R7 subset, R33, R34 markers)
- [x] 2.3 Record R38 in `research.md`; tick T018 and T019 in `tasks.md`

## 3. Proof

- [x] 3.1 `debug`, `debug-clang`, and `shared` presets build with no warnings and pass

## 4. Review of 8be01e1 (takeover)

- [x] 4.1 Accept a grapheme ending in ZWJ (before a sequence, at the end of a write, at the end of an expected row); reject the next glyph unless a CUP came first; tests watched failing first
- [x] 4.2 Parse parameters with checked accumulation and no digit cap; six-digit CUPs on both axes, overflow and wrap rejections; a mutant with unchecked accumulation fails them
- [x] 4.3 Make the test constants `constexpr` (Clang 19 minimum); update R38 and the header docs
