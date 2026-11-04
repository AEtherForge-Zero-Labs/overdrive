## Why

Issue #11 (OVD-10, tasks T031 and T032) turns the merged pieces (the
allocator, the output buffer, the grid and canvas, and the diff) into the
public terminal and frame API. FR-002, FR-012, FR-014, and FR-022 apply,
as do R21 to R25, R31, and R35. Every caller of Overdrive goes through
it. The system backend (OVD-11) and the steady-state test and benchmark
(OVD-13) are blocked on it.

## What Changes

- Add `src/internal.h`: `struct OvdTerminal`, private to the terminal
  module.
- Add `src/terminal.c` with the following:
  - `ovd_terminal_config_default`;
  - the headless backend's `ovd_terminal_create` and
    `ovd_terminal_destroy`;
  - `ovd_terminal_size`, `_color_depth`, `_set_size`, and `_invalidate`;
  - `ovd_frame_begin`, `_end`, and `_abandon`, with one sink call per
    frame and optional synchronized-output markers.
- Until OVD-11 lands, `ovd_terminal_create` validates a system-backend
  config like any other, then returns `OVD_ERROR_UNSUPPORTED` (R44).
- Add `tests/test_terminal.c` and register it with CTest.
- Append the source to the library target and the test to
  `tests/CMakeLists.txt`.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements remain in Spec Kit. This transaction
implements T031 and T032 without changing their requirements.

### Modified Capabilities

None.

## Impact

This change touches only:
- the paths issue #11 owns;
- append-only lines in the two CMake files;
- the T031 and T032 ticks in `specs/001-render-core/tasks.md`;
- one decision entry, R44, in `specs/001-render-core/research.md`.

No public header changes.
