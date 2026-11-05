## Why

The independent review of `input_demo` and part 2 of the manual checklist found two documentation defects. It also made three notes about the checklist.

- **`--no-input` used a full processor core.** The checklist says the program waits without using the processor. With `--no-input`, it paced its redraws by reading the clock in a loop, so this was false. A pty run measured 2.00 s of CPU in 2 s.
- **The expected key lines were wrong.** The checklist showed `key   Ctrl+c`, but the demo prints a character key with its code point: `key   Ctrl+c  (U+0063)`. Two rows of its table also disagreed with each other.

The notes:
- A `sanitize` build changes the `segv` crash.
- The paste sample text should not contain `q` or `c`.
- A pasted line feed shows as `Ctrl+j`.

## What Changes

- **`examples/input_demo.c`.** With `--no-input`, the demo now sleeps about 100 ms between redraws: `nanosleep` on POSIX, `Sleep` on Windows. This supersedes design D5 of the archived change `2026-11-05-i57-input-demo`, which paced the redraws with a clock. ISO C's only sleep is `thrd_sleep`. Neither Apple's libc nor MinGW-w64 provides `<threads.h>`: clang, gcc-14 and the MinGW cross compiler each failed on `#include <threads.h>`.
- **`docs/validation/manual-checklist.md`.**
  - The key lines match the demo's output.
  - The waiting bullet says what `--no-input` does.
  - Part 2 says not to use a `sanitize` build.
  - I5 gives sample text without `q` or `c`, and allows `Ctrl+j` for a pasted line break.

## Impact

The changes are to an example and to documentation; the library is unchanged.
