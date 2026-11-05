## Why

Issue #43, from the review of #42 (OVD-11): only one automated test covers
the system backend, `test_system_notty`, and it covers only the refused
path, where stdout is not a terminal. Nothing automated checks create's
enter sequence, destroy's exit sequence and mode restore, or create's
rollbacks. For example, deleting the `undo_open` call in `src/terminal.c`
leaves input echo off after a failed create, and every test still
passes. #42 observed each of these behaviors by hand in scratch probes,
and none of those probes is a regression test.

## What Changes

- Add `tests/test_system_pty.c`, POSIX only. The test opens its own
  pseudo-terminal, puts the slave on stdin and stdout, and reads what the
  library writes from the master. It checks the following:
  - the enter and exit bytes (R26, R46), with and without
    `alternate_screen`;
  - termios `ECHO` is off after create, and destroy restores the
    terminal's modes exactly;
  - create's rollbacks restore the modes and leave no live block when:
    - every allocation, or any single allocation, is refused (R28 step
      6);
    - the enter write fails (step 7, `OVD_ERROR_IO`);
    - the size reads 0 in either dimension (step 5,
      `OVD_ERROR_PLATFORM`);
  - each frame re-reads the size (FR-014): a resize between frames gives
    a full redraw at the new size, and a 0 size fails that frame's begin;
  - a 1×1 terminal works.
- Register the test in `tests/CMakeLists.txt` on POSIX only (`UNIX`), with
  append-only lines.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements stay in Spec Kit. This change adds a
regression test for behavior that FR-010, FR-011, and FR-014 and
research R25 to R28 and R46 already require. It changes no requirement.

### Modified Capabilities

None.

## Impact

- New file `tests/test_system_pty.c`. Test code may include OS headers;
  `src/` still may not.
- Append-only lines in `tests/CMakeLists.txt`.
- No library source, public header, or build option changes. Windows
  builds, including the MinGW cross presets, do not build the test.
  ConPTY coverage is out of scope (issue #43).
