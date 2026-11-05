## Why

Issue #53 (OVD-16) is feature 002's input decoder: the module that turns
untrusted terminal bytes into typed events (keys, SGR mouse, paste,
focus). OVD-17's headless input and OVD-18's system input both feed it,
so it comes before either (plan.md, Workstreams; Art. V). SC-001 and
SC-002 are measured against it alone.

## What Changes

- New private module `src/decode.{h,c}`, `OvdDecoder` (T108; R104,
  R105, R109, R103):
  - an ECMA-48 syntax layer for C0 controls, `ESC` + byte, `SS3` + byte
    and every CSI form, with a 64-byte parameter area; a longer sequence
    is consumed to its final byte and discarded;
  - a dispatch table from the parsed sequence to an event: the R105 keys
    with modifiers, SGR mouse, focus, and the paste markers; anything
    else is discarded;
  - paste mode: text chunks of repaired UTF-8 into a caller-provided
    buffer, holding back at most a 5-byte end-marker prefix and one
    partial scalar;
  - the escape delay as an argument: each call takes the caller's
    monotonic time in milliseconds. The module reads no clock.
- `src/unicode.{h,c}` gain one private function,
  `ovd_unicode_decode_utf8`. It exposes the decoder that
  `ovd_unicode_next_grapheme` already uses, and reports a sequence cut
  short by the end of input apart from an ill-formed one. The grapheme
  iterator's behavior does not change (design D1).
- Tests: `tests/test_decode.c` (the SC-001 table in readable notation,
  every row split at every byte boundary; T105–T107) and
  `tests/test_decode_fuzz.c` (SC-002: at least 10⁷ seeded bytes, with
  the invariants checked after every call; T109).
- Build: `src/decode.c` is appended to the library's sources. Both tests
  are appended to `tests/CMakeLists.txt` and compile in the modules they
  test, as `test_unicode` does.
- The T105–T109 ticks in `specs/002-input-restore/tasks.md`.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

- No public API change: `decode.h` is private and every new symbol
  carries `ovd_` (Art. XIV, R12).
- `src/unicode.{h,c}` are outside #53's owned files. No open issue owns
  them, and plan.md lists `unicode.c` as reused (design D1).
- No platform code and no terminal behavior change. The decoder has no
  caller until OVD-17.
