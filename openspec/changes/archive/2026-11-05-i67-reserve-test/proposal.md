## Why

R112 has create block the handled signals on the calling thread, from reserve until the record is armed. A handled signal raised on that thread in between must then wait, and restore once it arrives. No test checked this: a mutant that skips the blocking in `ovd_platform_restore_reserve` passed the whole suite.

## What Changes

`tests/test_restore_pty.c` gains the case "SIGTERM during create". A child creates a system terminal with an allocator that raises `SIGTERM` at its first call. That call falls between the mode change and the arm (`src/terminal.c`, `src/terminal_system.c`).

The child must:
- end by `SIGTERM`, with the same status as a control child that raises `SIGTERM`;
- never reach the code after create;
- leave the pty's modes restored;
- have written the enter sequence once and the exit sequence once, ending with the full exit sequence.

On the mutant, the case failed on `modes_restored`. With the real code, it passed.

## Impact

Tests only. The library is unchanged.
