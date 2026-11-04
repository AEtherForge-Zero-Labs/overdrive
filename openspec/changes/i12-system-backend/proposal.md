## Why

Issue #12 (OVD-11, tasks T033 and T034) adds the system backend: the
process's own console behind the terminal API that OVD-10 merged with
only a headless backend. FR-010, FR-011, and FR-014 apply, as do R25 to
R29. Until it lands, `ovd_terminal_create` declines every system config
with `OVD_ERROR_UNSUPPORTED` (R44), so no application can draw to a real
terminal. The example and the manual checklist (OVD-12) and the
documentation pass (OVD-14) are blocked on it.

## What Changes

- Add `src/terminal_system.c`, the only terminal file that calls
  `platform.h`. It covers:
  - create's R28 steps 1 to 5, plus color detection;
  - the step 7 enter sequence, with its rollback;
  - destroy's exit sequence and mode restore;
  - the per-frame size read and write.
- Change `src/terminal.c` at the points where the backend matters:
  - create stops returning `OVD_ERROR_UNSUPPORTED` and runs R28 in order,
    undoing completed steps on failure;
  - destroy writes the exit sequence and restores the modes before
    freeing;
  - begin re-reads the size on every frame (FR-014);
  - end writes the frame to stdout instead of calling a sink.
- Add the system state and the `ovd_terminal_system_*` declarations to
  `src/internal.h`.
- Add `tests/test_system_notty.c` (T034). A CMake driver runs it with
  stdout redirected to a file and checks two things: create returns
  `OVD_ERROR_NOT_A_TERMINAL`, and the file is empty.
- In `tests/test_terminal.c`, drop R44's "system → `OVD_ERROR_UNSUPPORTED`"
  check. Once the backend exists, that check would acquire a real
  terminal whenever the test runs on a tty. Its "headless_size is not
  validated for system" half moves to the new test.
- Append the source to the library target and the test to
  `tests/CMakeLists.txt`.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements remain in Spec Kit. This transaction
implements T033 and T034 without changing their requirements.

### Modified Capabilities

None.

## Impact

This change touches only:
- the paths issue #12 owns, amended append-style to add `src/terminal.c`,
  `src/internal.h`, and `tests/test_terminal.c` (R44; OVD-10 is closed
  and no open issue owns them);
- append-only lines in the two CMake files;
- the T033 and T034 ticks in `specs/001-render-core/tasks.md`;
- one decision entry, R46, in `specs/001-render-core/research.md`. R45
  is taken by open PR #40.

No public header changes. Applications that create a system terminal
now get one, where they used to get `OVD_ERROR_UNSUPPORTED`.
