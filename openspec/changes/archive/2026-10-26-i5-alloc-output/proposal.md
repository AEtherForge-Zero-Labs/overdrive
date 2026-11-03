## Why

Issue #5 (OVD-04, tasks T016 and T017) provides the two internal pieces
every later allocating module needs: allocator dispatch with
overflow-checked sizes (R4, R32) and the fixed-capacity output buffer the
differential renderer writes a frame into (R30, FR-022). The canvas,
diff, and terminal issues (OVD-07, OVD-08, OVD-10) are blocked on it.

## What Changes

- Add `src/alloc.h`/`src/alloc.c`: the default `malloc`/`free` allocator,
  validation of a user `OvdAllocator`, overflow-checked block allocation,
  release with the requested size, and an all-or-nothing batch
  allocation that a resize uses before releasing old blocks (R31, R32).
- Add `src/output.h`/`src/output.c`: `OvdOutput`, a byte buffer whose
  capacity is fixed when its storage is adopted, with bounded
  all-or-nothing appends, reset for reuse, and no allocation of its own
  (R30).
- Add a counting and failing test allocator in `tests/support/` and
  `tests/test_alloc.c`, registered with CTest.
- Append the two sources to the library target and the test to
  `tests/CMakeLists.txt`.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements remain in Spec Kit; this transaction
implements T016 and T017 without changing their requirements.

### Modified Capabilities

None.

## Impact

Only the paths issue #5 owns, append-only lines in the two CMake files,
the T016/T017 ticks in `specs/001-render-core/tasks.md`, and one decision
entry in `specs/001-render-core/research.md`. No public header changes.
