## 1. Contract

- [x] 1.1 `widgets.h` into `include/overdrive/`, unchanged. It is included from `overdrive.h`, listed in `overdrive_public_headers` and in the install-layout header row, and covered by the enum-size and `ovd_text` checks in `test_headers` (T301).

## 2. Tests first

- [x] 2.1 `tests/support/canvas_screen.{c,h}` (T302).
- [x] 2.2 The encoder cases in `test_unicode` (T303). Watch them fail.
- [x] 2.3 `test_textfit` (T304). Watch it fail. It first failed only for want of `src/textfit.c`; a mutant cut rule (keeping graphemes up to w rather than w - 1) then failed six cases.
- [x] 2.4 `test_widget` (T305). Watch it fail.

## 3. Implementation

- [x] 3.1 `ovd_unicode_encode_utf8` (T306).
- [x] 3.2 `src/textfit.{c,h}` (T307).
- [x] 3.3 `src/widget.{c,h}` (T308). No OVD-25 task tests the internal clip and combine helpers, so `test_textfit`, which already compiles internal sources in, also checks them; a mutant of each failed.

## 4. Proof

- [x] 4.1 `debug`, `shared`, `sanitize`, and `windows-cross-debug` from clean, with 0 warnings and every test passing, including `consumer_*` (T309).
