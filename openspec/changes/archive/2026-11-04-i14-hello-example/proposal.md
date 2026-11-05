## Why

Issue #14 (OVD-12) covers tasks T037 and T038, for User Story 2
("Acquire and restore a real terminal"). Its independent test is the
`hello_overdrive` example, run by hand in Windows Terminal and in one
POSIX terminal while following a written checklist. SC-007 needs both
the program and the checklist, and neither exists. The system backend
(OVD-11) and box drawing (OVD-09) that the example uses are merged.

## What Changes

- Add `examples/hello_overdrive.c` (T037). It acquires the process's
  terminal and, for a few seconds (5 by default, or 1 to 60 given as
  an argument), draws frames showing:
  - the brand palette, from `OVD-IDENTITY-001` §6.1;
  - the name and the tagline "Every frame matters.";
  - the five border styles;
  - a frame counter and progress bar that change every frame;
  - the terminal's current size and color depth.

  It stops early on Ctrl+C, then restores the terminal and prints a
  one-line summary. Any failure is reported on stderr, after the
  terminal is restored, with exit status 1.

  Earlier decisions defer some real-terminal checks to this checklist:
  research §E's "Unverified" list, R38, R40, and the comments in
  `src/terminal_system.c` and `src/platform/platform_windows.c`. So the
  checklist can reach them, the example also takes
  `--synchronized-output` and `--no-alternate-screen`, and draws R38's
  ZWJ case on a row of its own.
- Add `examples/CMakeLists.txt` with the `hello_overdrive` target.
- Turn `OVERDRIVE_BUILD_EXAMPLES` on in the base configure preset,
  which every shared preset inherits. This lets the issue's proof
  command, `cmake --build --preset debug --target hello_overdrive`,
  find the target, and CI then compiles the example on each platform
  it builds.
- Append a test to `tests/CMakeLists.txt`, `hello_overdrive_notty`. It
  runs the example with stdout redirected to a file and requires exit
  status 1, an empty file, and `OVD_ERROR_NOT_A_TERMINAL` on stderr.
  This is US2 scenario 3 at the program level.
- Add `docs/validation/manual-checklist.md` (T038). It gives the steps
  and expected results for SC-007 in Windows Terminal and one POSIX
  terminal. It also covers the three checks the issue's comment asks
  for, and the deferred checks above. A results template records each
  terminal's name and version.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements stay in Spec Kit. This transaction
implements T037 and T038 without changing their requirements.

### Modified Capabilities

None.

## Impact

This change touches only:
- the paths issue #14 owns (`examples/`,
  `docs/validation/manual-checklist.md`);
- append-only lines in `tests/CMakeLists.txt`;
- one value in `CMakePresets.json`. OVD-01 owns that file, but OVD-01
  is closed and no open issue owns the file. Without this change the
  issue's own proof command cannot pass;
- the T037 and T038 ticks in `specs/001-render-core/tasks.md`;
- one decision entry, R48, in `specs/001-render-core/research.md`.

There are no library source changes and no public header changes. A
consumer that adds Overdrive with `add_subdirectory` is unaffected,
because the option's default in `CMakeLists.txt` stays off. Only the
repo's own presets turn it on.
