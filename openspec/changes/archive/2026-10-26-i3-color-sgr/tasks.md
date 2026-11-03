## 1. Plan

- [x] 1.1 Record the change and post the plan comment on #3.

## 2. Color

- [x] 2.1 Write `tests/test_color.c` and observe it fail (T010).
- [x] 2.2 Implement `src/color.h` and `src/color.c` (T011).
- [x] 2.3 Record R37 in `research.md`; tick T010 and T011.

## 3. Delivery evidence

- [x] 3.1 Run the debug, debug-clang, and shared presets with zero warnings.
- [x] 3.2 Reread the full diff; publish the PR with evidence.

## 4. Review follow-ups

- [x] 4.1 `src/color.h`: `OVD_COLOR_SGR_MAX_LENGTH` becomes `constexpr size_t` (Art. III), now that the Clang minimum is 19 (#37), and so does `src/color.c`'s `OVD_COLOR_ATTR_MASK` (`constexpr uint16_t`); the design note says so.
