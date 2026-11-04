## Context

See proposal.md for the motivation. The pieces are merged:
- `platform.h` (OVD-06, R40) does R28 steps 2 to 4 with its own
  rollback (`ovd_platform_enter`), undoes them (`ovd_platform_leave`),
  reads the size, writes all bytes, and reads the environment.
- `detect.h` maps the environment, plus the Windows virtual-terminal
  flag, to a color depth.
- `terminal.c` (OVD-10, R44) has the frame state machine. Its system
  branch returns `OVD_ERROR_UNSUPPORTED` after full validation.

The contract is fixed by terminal.h (create's order and errors,
destroy's exit sequence and restore order, size re-read in begin) and by
R25 to R29. No public header changes.

## Goals / Non-Goals

**Goals:**
- create and destroy exactly as terminal.h and R28 order them, with
  every failure undoing the completed steps in reverse;
- `OVD_ERROR_NOT_A_TERMINAL` before anything is written, changed, or
  allocated;
- the size re-read at every begin (FR-014), resizing only when it
  differs;
- one write per frame, through `ovd_platform_write` (FR-022);
- `terminal.c` stays free of `platform.h`: `terminal_system.c` is the
  only terminal file that calls the OS layer.

**Non-Goals:**
- input, signals (`SIGWINCH`, Ctrl-C), and restore at process exit:
  terminal.h says destroy is not async-signal-safe and that a process
  that exits without it leaves the terminal as create left it;
- detecting a second system terminal (R29);
- a pty-driven regression test of the enter and exit bytes. That is not
  in #12's owned paths; this transaction observes them in a scratch pty
  run as evidence and notes the gap.

## Decisions

- **`OvdTerminalSystem` is owned by `terminal_system.c`.** It is
  embedded in `struct OvdTerminal`, and only `terminal_system.c` reads or
  writes its fields. It holds:
  - the `OvdPlatformModes` that `ovd_platform_enter` recorded;
  - whether the alternate screen was requested.

  Its state lives from create to destroy, so it is an owning module
  (Art. IV). It is declared in `internal.h` so the terminal embeds it
  without another allocation, the same tension and resolution as
  `OvdOutput` (R39).
  *Rejected:* a backend function-pointer table (Art. VII; one system
  backend per build), and keeping `platform.h` calls in `terminal.c`
  (spreads the OS dependency over two files).
- **The system functions, one per lifecycle point:**
  - `open`: R28 step 1 (`OVD_ERROR_NOT_A_TERMINAL`, nothing written),
    steps 2 to 4 (`ovd_platform_enter`), and step 5 (size; on failure
    it undoes 4, 3, 2 and returns `OVD_ERROR_PLATFORM`).
  - `detect`: the color depth from the environment and the VT flag
    recorded by `open`. Called only when `detect_color_depth` is set.
  - `undo`: steps 4, 3, 2, for a step 6 failure.
  - `enter`: step 7, the enter sequence in one write. If the write
    fails, `enter` writes the exit sequence best-effort (alternate
    screen counted as entered whenever requested, cursor row from the
    size read at create), undoes 4, 3, 2, and returns `OVD_ERROR_IO`.
  - `close`: destroy's exit sequence in one write, then 4, 3, 2.
    Every step is attempted; nothing is reported.
  - `size` and `write`: the per-frame read and the frame write.
- **The byte sequences:**
  - Enter is `CSI ? 1049 h` (only when the alternate screen is
    requested), then `CSI ? 25 l`, then `CSI ? 7 l`.
  - Exit is `CSI 0 m`, `CSI ? 7 h`, and `CSI ? 25 h`. Then comes
    `CSI ? 1049 l` if the alternate screen was entered, or else
    `CSI <rows> ; 1 H`: column 0 of the last row, in the diff's
    cursor-move form.
  - Both are built in a fixed stack buffer whose capacity is checked
    with `static_assert` against the longest sequence. No caller byte is
    in either.
- **`terminal.c` changes only where the backend matters:**
  - Create runs R28 in order: system `open`, then depth (detected or
    configured), then the requests and one allocation batch (on
    failure, `undo`), then system `enter`. If `enter` fails, the blocks
    are freed and create returns `OVD_ERROR_IO`. `*out` is written only
    on success.
  - Destroy abandons the frame, then calls `close` with the last
    frame's rows, then frees.
  - Begin computes the target size: the pending size or the current one
    for headless, a fresh `size` read for system (failure is
    `OVD_ERROR_PLATFORM`, R25). It resizes only if the target differs.
    A failed begin changes neither the current size nor the pending
    one.
  - End writes the frame with `write` for system. A failed write is
    `OVD_ERROR_IO` with the screen unknown, as a sink failure is.
- **"The last frame's size" (R26)** is the size of the last frame passed
  to `ovd_frame_end`, whatever the outcome: the frame ended, and some of
  its bytes may have reached the screen. Until a frame ends it is the
  size read at create. It is kept as `last_rows` in `struct
  OvdTerminal`, the terminal's own field, and passed to `close`.
- **Detection runs once, at create,** after step 4, because the Windows
  VT flag is known only then. It does not re-run per frame: terminal.h
  says the depth is detected at create.
- **The system backend ignores `sink`, `sink_context`, and
  `headless_size`,** which terminal.h marks headless-only.
- **The test (T034)** is a program plus a CMake driver generated in
  `tests/CMakeLists.txt`, in the style of `check_exports_negative`. The
  driver runs the program, through `CMAKE_CROSSCOMPILING_EMULATOR` when
  one is set, with `OUTPUT_FILE`. It fails unless the exit code is 0 and
  the file exists and is empty. The program, using the public API and
  the counting allocator, checks the following:
  - create returns `OVD_ERROR_NOT_A_TERMINAL` for several system
    configs, including one with a 0×0 `headless_size`, which proves
    headless-only fields are not validated for system;
  - `*out` is untouched and the allocator is never called;
  - argument errors still win over the terminal check (R44's order).
  It prints only to stderr.
  *Rejected:* `freopen` of stdout inside the program. Whether it
  replaces descriptor 1 on POSIX, and the Win32 standard handle on
  Windows, is ASSUMED, not documented here; the driver's redirection is
  what a user's `> file` does.

## Risks / Trade-offs

- [The enter and exit bytes have no regression test] → They are
  observed in a scratch pty run on macOS as evidence (bytes, echo
  restore, 1×1, 0×0, resize between frames). A pty test is noted for a
  follow-up issue. Real terminals are checked by the human through
  OVD-12's checklist.
- [Copying `OvdPlatformModes` from the stack into the terminal after
  allocation] → It is plain bytes (termios or DWORDs). Allocation has
  to come after the size read (R28 steps 5 and 6), so the modes are
  recorded before the struct exists.
- [A write that fails partway through a frame] → The screen becomes
  unknown, so the next frame is a full redraw, as with a sink failure.
- [The research entry number] → R45 is taken by open PR #40, so this
  change logs R46. If it merges first, R45 is missing until #40 lands.

## Assumptions (R46, labeled `assumption`)

These are the decisions above that terminal.h and R25 to R29 leave open:
- "the last frame" is the last frame ended, whatever its outcome;
- a system begin whose read size equals the current size does not
  resize or redraw, and a failed begin changes no size;
- detection runs once, at create;
- the exit sequence's cursor move is `CSI <rows> ; 1 H`;
- a failed system write in end is `OVD_ERROR_IO` with the screen
  unknown.
