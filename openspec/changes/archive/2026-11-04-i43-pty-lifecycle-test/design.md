## Context

See proposal.md for why. This test drives the following code, all merged:
- create and destroy: `src/terminal.c` (R28 steps 1 to 7);
- the system backend: `src/terminal_system.c` (the R26 sequences, and R46's
  exit row);
- the POSIX platform layer: `src/platform/platform_posix.c` (stdin's
  `ECHO`, and `TIOCGWINSZ` on stdout).

The backend uses only the process's own stdin and stdout. A test that
reaches it must therefore put a terminal on fds 0 and 1, and must never
let the library touch the developer's terminal. `test_system_notty`
covers the stdout-not-a-terminal path, and this test leaves it alone.

Facts measured for this design, in a scratch probe on macOS 27.2 with
Apple clang 21 (VERIFIED):
- a new pty reads as 0×0;
- a slave opened `O_RDONLY` passes `isatty` and `TIOCGWINSZ`, and
  `write` on it fails with `EBADF`;
- bytes written to the slave can be read from the master as soon as the
  write returns;
- after the master closes, `isatty`, `tcgetattr`, `tcsetattr`, and
  `TIOCGWINSZ` on the slave still succeed, and only `write` fails
  (`EIO`).

No Linux machine was available, so the Linux behavior is INFERRED. The
ubuntu-24.04 CI rows measure it.

## Goals / Non-Goals

**Goals:**
- Every item in issue #43's list, checked by a test, on Linux and macOS.
- Leave the developer's terminal untouched, even when a check fails.
- No waiting on timers to decide what was written: the test must not be
  flaky under CI load.
- Show the test is not vacuous: each part fails when the behavior it
  covers is removed.

**Non-Goals:**
- Windows or ConPTY (the issue puts this out of scope; it stays manual,
  OVD-12).
- Re-testing the renderer. `test_diff` covers it. This test checks that
  the system backend writes what the renderer produced, at the size it
  read.
- Color detection from the environment (`test_detect` covers it). The
  frame cases turn detection off, so their depth is fixed.
- Any change under `src/` or `include/`.

## Decisions

- **The fixture is a fresh pty for each case.** It opens the master with
  `posix_openpt(O_RDWR | O_NOCTTY)`, then calls `grantpt` and `unlockpt`,
  and opens `ptsname` with `O_RDWR | O_NOCTTY`. These calls are
  XSI-specified in POSIX.1-2024, hence `_XOPEN_SOURCE 700`.
  - **`O_NOCTTY`.** The pty never becomes the test's controlling
    terminal, so no `SIGHUP`, `SIGWINCH`, or `SIGTTOU` from it can reach
    the test.
  - **Slave modes.** The fixture turns `OPOST` off on the slave, so the
    master reads exactly the bytes the library wrote, and turns `ECHO`
    on, so turning it off is observable.
  - **Size.** Set with `TIOCSWINSZ` on the slave, and never left at the
    pty's default.

  *Rejected:* `openpty` and `forkpty`. They are not POSIX, they need
  `-lutil` on Linux, and their headers differ between Linux and macOS.
- **Descriptor safety.**
  - `main` makes sure fds 0, 1, and 2 are open, opening `/dev/null` if
    not, so a new pty can never land on them.
  - Each case saves the test's fds 0 and 1 with `dup`, then puts the
    slave on both with `dup2`.
  - Before create runs, the case checks with `fstat` that fds 0 and 1
    are the slave (same `st_rdev`). If they are not, it fails without
    calling the library.
  - Teardown restores both fds on every path, pass or fail.

  The library therefore only ever changes the pty's modes, and a
  failing check leaves nothing to restore on the developer's terminal.
- **Reading output with a sentinel, not a quiet period.** To check what
  the library wrote since the last check, the test does three things:
  1. it writes a sentinel (`\x01` … `\x02`) to the slave itself;
  2. it reads the master until the bytes end with that sentinel, with a
     5 s `poll` limit as a hang guard only;
  3. it compares everything before the sentinel to the expected bytes.

  Writes to one tty are ordered, and the library cannot emit a C0
  control other than ESC (canvas text replaces them, R7). So "wrote
  nothing" is an exact check, with no timing involved.
  *Rejected:* reading until a quiet period. It turns scheduler latency
  into false passes or flaky failures.
- **Where the expected bytes come from:**
  - **The enter and exit sequences** are spelled in the test from R26 and
    xterm's control-sequence reference, independently of
    `terminal_system.c`'s macros:
    - enter: `CSI ? 1049 h` (with `alternate_screen`), `CSI ? 25 l`,
      `CSI ? 7 l`;
    - exit: `CSI 0 m`, `CSI ? 7 h`, `CSI ? 25 h`, then `CSI ? 1049 l` or
      `CSI <rows> ; 1 H`, where rows is the last ended frame's (R46).
  - **Frame bytes** come from a headless terminal (R35) that runs the same
    frames, with the same depth and flags, and captures them through its
    sink. Both backends share the renderer, so equality shows the wiring:
    one write per frame, at the size read, in order. The renderer itself
    is `test_diff`'s.
  - **After a resize,** the frame's bytes are also fed to the replay
    oracle (`vt_replay`) at the new size. The redraw must be accepted
    there, and a marker in the new bottom-right cell must land.
- **Making the enter write fail: a read-only stdout.** The case opens
  the slave a second time with `O_RDONLY` and puts that on fd 1. Steps 1
  and 5 read through it, and `write` fails with `EBADF` (POSIX `write`:
  "not a valid file descriptor open for writing"). Step 7 then returns
  `OVD_ERROR_IO`. Two counts show that step 6 ran and was undone:
  allocate calls 4, release calls 4. Rejected alternatives:
  - *Closing the master first* (the issue's suggestion, and what #42 did
    on macOS): on Linux, closing the master hangs up the slave
    (`pty_close` → `tty_vhangup`, INFERRED from the kernel source, not
    run). Every ioctl then fails, so create would stop at step 1 with
    `OVD_ERROR_NOT_A_TERMINAL` and never reach step 7.
  - *A non-blocking stdout over a full pty buffer:* on Linux the buffer
    drains to the master asynchronously, so whether the write fails is
    a race.
- **Allocation refusals.** `ovd_alloc_all` takes create's four blocks
  one call at a time. The test refuses each call in turn (1 to 4), and
  then every call. Each case checks the following:
  - create returns `OVD_ERROR_ALLOCATION` and leaves `*out` as it was;
  - the pty's modes are as they were;
  - no live block remains, and there are 0 contract violations;
  - nothing was written.
- **Zero sizes at create:** 0×0, 0×4, and 10×0. Each must return
  `OVD_ERROR_PLATFORM` with no allocator call and nothing written, and
  leave the modes restored (R25, step 5).
- **The frame sequence, in one terminal created at 10×4 without the
  alternate screen.** Each frame's bytes must equal the headless twin's:
  1. Frame 1 at 10×4 is a full redraw. Its content includes a wide glyph
     at the last column, which must be drawn as a space (render.h).
  2. The pty is resized to 12×5. Frame 2 is a full redraw at 12×5, and
     also passes the replay check.
  3. Frame 3 has the same content, and nothing is written.
  4. The pty is resized to 0×0. Begin returns `OVD_ERROR_PLATFORM`, the
     terminal's size stays 12×5, and nothing is written.
  5. Back at 12×5, frame 4 begins and changes a cell, and the pty is
     resized to 7×3 while the frame is open. The frame ends at 12×5 as a
     diff, not a redraw.
  6. Frame 5 is a full redraw at 7×3.
  7. Destroy writes the exit sequence with `CSI 3 ; 1 H`. The last ended
     frame's rows are 3, which differ from the create-time rows (4) and
     from 5, so R46 is actually checked.
- **1×1.** Create at 1×1 without the alternate screen and end one frame
  of the same content. The label and the wide glyph are clipped around
  the one cell, and the marker lands in it. The frame must equal its
  headless twin and pass the replay check. Destroy, and expect exit
  bytes ending in `CSI 1 ; 1 H`.
- **Mode checks compare the whole termios,** not just `ECHO`: the three
  flag words other than `c_lflag`, all of `c_lflag` except `ECHO`, every
  `c_cc` entry, and both speeds.
  - After create: `ECHO` is off and nothing else changed.
  - After destroy or a failed create: everything is as it was before
    create.
- **Registration.** `if(UNIX)` in `tests/CMakeLists.txt`, appended. The
  test uses the public API only, compiles `support/test_alloc.c`, links
  `overdrive_vt_replay`, and has the default 60 s test timeout. `UNIX`
  is false for the MinGW cross presets, so wine never runs it.
- **Non-vacuity.** Before delivery, each part is run against a scratch
  mutation of `src/` and must fail. The mutations are reverted, and the
  task list records them.

## Risks / Trade-offs

- [Linux pty behavior is INFERRED, not run in this session] → The
  ubuntu-24.04 rows in CI (gcc-14 debug, release, and shared; clang-19)
  run the test. The PR cites their results, and a Linux failure blocks
  the merge.
- [A failing check could leave a pty with echo off] → The pty is the
  test's own and closes at teardown. Only the pty's modes are ever
  changed, never the developer's terminal.
- [A library hang, or a missing sentinel, could block the test] → Every
  master read is bounded by `poll` with a 5 s limit, and ctest's 60 s
  timeout backs that up.
- [The pty's buffer is finite, and the test reads only after the library
  writes] → Frames stay at 12×5 cells or fewer, a few hundred bytes. The
  capture buffer is 64 KiB, and the test fails, rather than truncating,
  if it fills.
- [Frame bytes come from the same renderer] → This is intentional. The
  renderer is not under test here. The replay oracle independently
  checks the one frame whose size changed.
