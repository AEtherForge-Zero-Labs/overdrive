## Why

Issue #55 (OVD-18) makes input work on the system backend, on POSIX and
Windows (T114–T116). OVD-17 made it work on the headless backend. This
change adds:
- raw input mode;
- a wait that reads stdin;
- the resize wakeup;
- the report modes (mouse, bracketed paste, focus).

T103's interim `OVD_ERROR_UNSUPPORTED` for the system backend goes.

## What Changes

- `src/platform/platform.h`, `platform_posix.c`, `platform_windows.c`:
  - a raw-input step after echo, read before it changes and restored in
    reverse (R106 on POSIX, R107 on Windows);
  - on POSIX, a resize-wakeup step: a self-pipe and a `SIGWINCH`
    handler (R111);
  - a bounded, non-blocking read;
  - a wait with a millisecond timeout;
  - a monotonic millisecond clock.
- `src/terminal_system.c`, `src/internal.h`:
  - with input on, create requires stdin to be a terminal (R115);
  - the enter sequence turns the requested reports on (R116);
  - the exit sequence starts with CAN, then turns those reports off in
    reverse (terminal.h's destroy contract);
  - wrappers for wait, read and clock, so `terminal_system.c` stays the
    only terminal file that calls the platform layer.
- `src/input.c`:
  - the system wait loop reads, decodes at the time of the read, and
    reports a resize only when the size differs from the last one
    reported (R111).
- `src/terminal.c`: the interim error is removed, and create passes the
  input options to the system backend. This is the one-line consequence
  of T116 in a file no open issue owns (see design D6).
- Tests:
  - `tests/test_system_pty.c` covers T114;
  - `tests/test_terminal.c` drops its interim assertion. A valid system
    config with input on would now acquire the test's own terminal
    (design D6).
- `specs/002-input-restore/plan.md`: the terminal-mode table is checked
  against the code (T116). `research.md` gains R120 for decisions made at
  implementation. T114–T116 are ticked.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

- Public API: no new symbols, no header change. With input on, a system
  terminal now succeeds where it returned `OVD_ERROR_UNSUPPORTED`.
- Every system terminal's exit sequence now starts with CAN (0x18), as
  terminal.h's destroy comment already says.
- Process-wide state, as FR-036 allows: on POSIX, the self-pipe and the
  `SIGWINCH` handler, shared by every system terminal with input on.
- Not verifiable here: Windows console input, and any real terminal.
  These are UNVERIFIED in the PR; the manual checklist after OVD-20
  covers them.
