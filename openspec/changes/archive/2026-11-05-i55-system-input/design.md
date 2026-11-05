## Context

OVD-17 built the input state (queue, chunk buffer, decoder) and the
headless wait. The system backend needs four things the platform layer
does not provide yet: raw input mode, a wait on stdin, a bounded read,
and a resize wakeup.
- `terminal_system.c` stays the only terminal file that calls the
  platform layer (internal.h).
- `input.c` reaches the platform through it.

## Decisions

### D1. The platform layer gains steps, a wait, a read, and a clock

`ovd_platform_enter(modes, input, mouse)` runs R28 steps 2 to 4 as
before. With `input`, it then runs:
- 4b, raw input (R106, R107);
- on POSIX, 4c, the resize wakeup (D2).

Each step is recorded in `OvdPlatformModes.steps` with the value read
before it, and `ovd_platform_leave` undoes them in reverse:
4c, 4b, 4, 3, 2. A failed step undoes the completed ones, as now.

New functions:
- `ovd_platform_input_is_terminal`;
- `ovd_platform_input_wait(modes, timeout_ms, *ready)`, where ready is
  stdin readable and, on POSIX, a resize signalled;
- `ovd_platform_input_read(modes, bytes, capacity, *length, *resized)`,
  non-blocking and bounded by capacity. On Windows a resize record sets
  `*resized`;
- `ovd_platform_clock_ms`, monotonic: POSIX `clock_gettime(CLOCK_MONOTONIC)`,
  Windows `GetTickCount64`.

POSIX saves two termios, the one before echo and the one before raw.
Both fit `OVD_PLATFORM_SAVED_SIZE`, which a `static_assert` checks.

### D2. The POSIX resize wakeup is process-wide, installed once

The handler cannot reach a terminal, so the pipe's descriptors and the
previous `SIGWINCH` action are static (FR-036). This follows
terminal.h's "only the first system terminal that is alive installs
them":
- the first system terminal with input on opens the pipe and installs
  the handler, and later ones share it through a use count;
- the last destroy puts the previous action back, but only if the
  current one is still Overdrive's (FR-032), and then closes the pipe;
- reinstalling would make the handler chain to itself, so it is never
  done.

The handler:
- saves `errno`;
- writes one byte when the write descriptor is valid;
- calls the previous action: `sa_sigaction` with `SA_SIGINFO`, else
  `sa_handler` unless it is `SIG_DFL` or `SIG_IGN`;
- restores `errno`.

`write` and `sigaction` are async-signal-safe (POSIX §2.4.3).

The pipe is made with `pipe` and `fcntl` (`O_NONBLOCK`, `FD_CLOEXEC`).
`pipe2` is POSIX.1-2024, but the macOS SDK does not declare it
(ASSUMED, as for `ppoll` in R111).

The write descriptor is set to -1 before the pipe is closed, so a
handler chained by the application after destroy writes nothing. A
signal racing that store can still write to a closed descriptor, which
fails with `EBADF`.

A limit: when two system terminals with input on wait at the same time,
one resize wakes one of them. terminal.h already says only one system
terminal may render.

### D3. The system wait reads, decodes at read time, and coalesces resizes

Each pass:
1. Report an unreported resize.
2. Decode the queue at the current clock.
3. If there is no event, wait on the platform until the earlier of the
   caller's timeout and the held bytes' deadline.
4. On a wakeup, read into the queue and stamp the clock.

The queue is empty whenever a read happens, because `next_event`
returns false only when it has consumed everything. A read therefore
takes up to `OVD_INPUT_QUEUE_SIZE` bytes, and the decoder sees each
byte at the time it was read.

A timeout of 0 still makes one non-blocking pass, so bytes that are
already available are returned (SC-005).

A wakeup or resize record re-reads the size. A size that differs from
the last one reported becomes one event (R111), and several wakeups
before a wait coalesce into one. A size that cannot be read, or reads
as 0, gives no event. The last size reported starts as the size read at
create.

`EINTR` from the wait is not an error: the pass recomputes the time
left. A failed wait or read is `OVD_ERROR_PLATFORM` (input.h).

### D4. Windows reads console input records

`ovd_platform_input_read` works in four steps:
- It calls `GetNumberOfConsoleInputEvents` and, only when records are
  unread, `ReadConsoleInputW`. `ReadConsoleInputW` "does not return
  until at least one input record has been read", so it is not called
  on an empty buffer.
- It reads at most `capacity / 12` records, and at most 64 per call.
- A key-down `KEY_EVENT` record's `UnicodeChar` is converted to UTF-8,
  `wRepeatCount` times, with the repeat count bounded by the space
  left. A high surrogate waits for its low half across calls; an
  unpaired surrogate becomes U+FFFD.
- `WINDOW_BUFFER_SIZE_EVENT` sets `*resized`. Other records are
  dropped.

The wait is `WaitForSingleObject` on the input handle: console input is
a waitable object (VERIFIED). That key records carry the VT sequence's
UTF-16 units under `ENABLE_VIRTUAL_TERMINAL_INPUT` remains ASSUMED
(R107). Microsoft's page names ReadFile and ReadConsole for that flag,
not ReadConsoleInput, and the manual checklist verifies it.

### D5. The sequences

Enter appends, each only if on:
- `CSI ? 1002 h` and `CSI ? 1006 h` for mouse;
- `CSI ? 2004 h` for bracketed paste;
- `CSI ? 1004 h` for focus.

Exit follows terminal.h's destroy order:
1. CAN.
2. SGR 0.
3. The reports that were on, turned off in reverse order.
4. `CSI ? 7 h`, then `CSI ? 25 h`.
5. Leave the alternate screen, or move to the last row.

CAN is written on every exit, not only with input on: the destroy
contract lists it unconditionally, and it cancels any sequence a signal
cut short (R112). The sequence buffer grows to fit the longest exit,
and a `static_assert` checks it.

### D6. Files beyond the issue's list

T116's own wording reaches four files beyond the list, each with the
smallest edit:
- `src/terminal.c`: remove the interim error and pass the input options
  to `ovd_terminal_system_open`.
- `tests/test_terminal.c`: its interim assertion (a system terminal with
  input on → `OVD_ERROR_UNSUPPORTED`) becomes false. Worse, a passing
  create there would acquire the developer's terminal. The assertion is
  removed, and `test_system_pty` covers the system backend with input on
  instead.
- `specs/002-input-restore/plan.md`: T116 requires the terminal-mode
  table to match the code.
- `specs/002-input-restore/research.md`: R120 records D2 to D5.

No open issue owns these lines: OVD-19 owns `terminal_system.c` and the
platform files after this one, and none of these four.

### D7. Tests

`tests/test_system_pty.c` covers T114:
- termios during the run and after destroy;
- `q`, an arrow, and 0x03 written to the master as events;
- `SIGWINCH` via `kill(getpid(), SIGWINCH)`: one event, two coalesced
  into one, and 0×N giving none;
- timing, using a forked child that writes to the master while the
  parent waits;
- stdin on `/dev/null`;
- feed returning `OVD_ERROR_UNSUPPORTED`;
- the report bytes;
- an application `SIGWINCH` handler, which runs and is back after
  destroy.

Create's allocation refusals with input on also check that raw mode and
the handler are undone.

## Risks / Trade-offs

- [Windows behaviour is not run here.] It is cross-compiled under the
  `-Werror` presets and run under Wine only as far as the suite reaches
  it. Console input is UNVERIFIED until the manual checklist.
- [SA_RESTART may restart poll after SIGWINCH.] POSIX: "the duration of
  the timeout following the restart is set to an unspecified value that
  does not exceed the original". The self-pipe makes the restarted poll
  return at once, so the wakeup does not depend on it (R111).
