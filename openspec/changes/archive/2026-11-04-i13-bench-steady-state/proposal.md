## Why

Issue #13 (OVD-13) covers tasks T035 and T036, for User Story 4
("Measure what a frame costs"). Art. XV forbids any performance claim
without a measurement behind it. Two success criteria name their
evidence:
- SC-003 (one-cell change ≤ 64 bytes) names the benchmark;
- SC-004 (0 allocator calls in frames of the current size) names the
  allocation test.

SC-008 (time in `ovd_frame_end` for a full 200×60 redraw) needs a first
measurement before anyone can set a target. Neither tool exists yet.
OVD-14 (#15), the documentation pass, is blocked on them.

## What Changes

- Add `tests/test_steady_state.c` (T035). Through the public API with
  the counting allocator, it checks two cases:
  - after create, the first frame and 100 more make 0 allocate and 0
    release calls;
  - after a resize, the resizing frame allocates, and the 100 frames
    after it make 0 calls again.

  The frames draw adversarial content and cover these terminal states:
  full redraws, sink failures, abandoned frames, misuse calls, and a
  `ovd_terminal_set_size` to the current size. The test runs at
  several sizes, at every color depth, with and without synchronized
  output.
- Add `bench/render_benchmark.c` and `bench/CMakeLists.txt` (T036). The
  benchmark drives a headless 200×60 truecolor terminal through four
  scenarios: full redraw, one-cell change, scrolling text region, and
  no change. For each it prints bytes per frame and timing statistics
  for `ovd_frame_end`. It also measures SC-003's worst case, a one-cell
  change at the bottom-right of a 999×999 terminal, and prints the
  library version, build type and flags, compiler, target, machine,
  and clock.
- Append `option(OVERDRIVE_BUILD_BENCHMARKS …)` and
  `add_subdirectory(bench)` to `CMakeLists.txt`, and register the test
  in `tests/CMakeLists.txt`.
- Add a `bench` configure preset and build preset to
  `CMakePresets.json`. The preset builds in Release with benchmarks on
  and tests off.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements stay in Spec Kit. This transaction
implements T035 and T036 without changing their requirements.

### Modified Capabilities

None.

## Impact

This change touches only:
- the paths issue #13 owns (`bench/`, `tests/test_steady_state.c`);
- append-only lines in `CMakeLists.txt` and `tests/CMakeLists.txt`;
- `CMakePresets.json`, owned by OVD-01. That issue is closed and no
  open issue owns the file, and T036 names the `bench` preset. The
  change is append-only: one configure preset and one build preset;
- the T035 and T036 ticks in `specs/001-render-core/tasks.md`;
- one decision entry, R47, in `specs/001-render-core/research.md`.

No library source changes and no public header changes. Default builds
are unchanged, because `OVERDRIVE_BUILD_BENCHMARKS` is off unless the
`bench` preset sets it.
