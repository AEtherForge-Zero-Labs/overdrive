## Why

Feature 002 (input and abnormal-exit restoration) is specified in
`specs/002-input-restore/`. Issue #52 (OVD-15) is its first step. It
freezes the public contract, so the decoder, input and restoration
issues build against fixed headers. It also adds the sanitizer build
that SC-002's fuzz test needs (Art. VIII: a declared tool before its
first use).

## What Changes

- `include/overdrive/input.h` (new), `terminal.h`, and `overdrive.h`
  become copies of `specs/002-input-restore/contracts/include/overdrive/`
  (T101). `input.h` joins `overdrive_public_headers`, so `test_headers`
  compiles it alone and twice and checks its includes.
- `ovd_terminal_config_default` sets the new fields: `input`, `mouse`,
  `bracketed_paste` and `focus_events` false, `escape_delay_ms` 50, and
  `restore_on_exit` true. `ovd_terminal_create` validates them (R115):
  - `mouse`, `bracketed_paste` or `focus_events` without `input`
    returns `OVD_ERROR_INVALID_ARGUMENT`;
  - `escape_delay_ms` outside 0–1000 returns the same error, even with
    input off.

  Each new check runs before anything is acquired (T102, T103).
- Interim results until later issues land (T103):
  - create with `input` true returns `OVD_ERROR_UNSUPPORTED`, until
    OVD-17 (headless) and OVD-18 (system) implement input;
  - `restore_on_exit` is accepted and has no effect until OVD-19.
- New `sanitize` configure, build and test presets (AddressSanitizer
  plus UndefinedBehaviorSanitizer, no recovery). CI gains a Linux GCC 14
  row and a macOS Apple Clang row, both covered by `ci-ok`.
  `.specify/memory/baseline.md` declares the sanitizer runtimes (T104).
- The T101–T104 ticks in `specs/002-input-restore/tasks.md`.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

- Public API: new header `input.h` and six new `OvdTerminalConfig`
  fields, with the contract frozen as specified (Art. XIV).
- The ABI changes because `OvdTerminalConfig` grows. 0.x promises no ABI.
- `ovd_input_wait` and `ovd_input_feed` are declared but not defined
  until OVD-17.
- The comments in `terminal.h` already describe behavior that OVD-17 to
  OVD-19 implement. 001 froze its headers the same way at OVD-01.
- No platform code changes.
