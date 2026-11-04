## Why

Issue #9 (OVD-08, tasks T027 and T028) provides the differential
renderer: the bytes that turn the screen the terminal is known to show
into the next frame (FR-020, FR-021, FR-024, FR-025; R8, R30, R33, R34).
The terminal and frame API (OVD-10) is blocked on it. The plan is the
issue's plan comment.

## What Changes

- Add `src/diff.h`/`src/diff.c`:
  - `ovd_diff_render(screen, frame, depth, synchronized, output)` appends
    one frame's output to an `OvdOutput`. `screen` is nullptr when the
    screen is unknown, which means a full redraw.
  - `ovd_diff_bound(size, synchronized, &bytes)` computes R30's
    worst-case output size, so the terminal can size its output block.
- Add `tests/test_diff.c`, which replays every output on the OVD-05
  oracle, and register it with CTest.
- Append `src/diff.c` to the library target and the test to
  `tests/CMakeLists.txt`.

## Capabilities

### New Capabilities

None in OpenSpec. The requirements stay in Spec Kit. This transaction
implements T027 and T028 without changing their requirements.

### Modified Capabilities

None.

## Impact

The change touches only:
- the paths issue #9 owns;
- append-only lines in the two CMake files;
- the T027 and T028 ticks in `specs/001-render-core/tasks.md`;
- one decision entry (R43) in `specs/001-render-core/research.md`.

No public header changes.
