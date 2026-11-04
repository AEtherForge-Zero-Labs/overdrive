## Why

Issue #10 (OVD-09, tasks T029 and T030) implements `ovd_canvas_box`, the
last public canvas function without an implementation (FR-034, User
Story 3). It is a prerequisite for OVD-12, the example and manual
checklist. The plan is the issue's plan comment.

## What Changes

- Add `src/box.c`, which implements `ovd_canvas_box` for the five
  `OvdBorder` styles. It writes only through `ovd_canvas_text`.
- Add `tests/test_box.c` and register it with CTest.
- Append `src/box.c` to the library target, and the test to
  `tests/CMakeLists.txt`.

## Capabilities

### New Capabilities

None in OpenSpec. The requirements remain in Spec Kit. This transaction
implements T029 and T030 without changing their requirements.

### Modified Capabilities

None.

## Impact

The change touches only:
- the paths issue #10 owns;
- append-only lines in the two CMake files;
- the T029 and T030 ticks in `specs/001-render-core/tasks.md`;
- one decision entry (R45) in `specs/001-render-core/research.md`.

No public header changes.
