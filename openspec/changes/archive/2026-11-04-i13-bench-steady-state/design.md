## Context

See proposal.md for the motivation. The pieces it measures are merged:
- the terminal and frame API with the headless backend (OVD-10, R44);
- the differential renderer and its output bound (OVD-08, R30, R43);
- the counting allocator in `tests/support/test_alloc.c` (OVD-04).

The headless sink receives each frame's bytes in one call, so a
benchmark can count bytes without a terminal. Per `terminal.h`, only
two calls allocate: `ovd_terminal_create`, and an `ovd_frame_begin`
whose size differs from the current buffers. SC-004 is that contract
observed from outside.

## Goals / Non-Goals

**Goals:**
- SC-004 tested through the public API, for any drawn content and in
  every terminal state a frame can pass through. The test must not pass
  vacuously: it also shows that the counter sees an allocation when one
  happens.
- US4 scenario 1: per-scenario bytes per frame and timing statistics at
  a headless 200×60, plus machine, compiler, and build type.
- SC-003's number measured at its worst case (999×999, bottom-right
  cell, both colors RGB with three-digit components, a 4-byte narrow
  glyph).
- SC-008's first measurement: `ovd_frame_end` for a full 200×60
  redraw.
- ISO C only in both programs (no OS header outside `src/platform/`).

**Non-Goals:**
- Setting the SC-008 target. The spec says a target follows the first
  measurement. This change produces that measurement and records it in
  R47. Choosing the number is a later decision.
- Throughput to a real terminal. The headless backend has no I/O, so
  terminal speed is outside every number reported here.
- Running the benchmark in CI, or gating merges on timing. `ci.yml` is
  OVD-01's, and shared CI runners give noisy timing.
- Comparing runs, or storing results in the repo.

## Decisions

- **The steady-state test uses only the public API and the counting
  allocator,** like `test_terminal`. It checks both `allocate_calls` and
  `release_calls`, since both are allocator calls. It also checks
  `violations == 0` and that `live_blocks` is unchanged.
- **Steady-state frames are drawn from a fixed xorshift seed.** The
  generator is the same as `test_diff`'s. Each frame does a random mix
  of the following:
  - fills and boxes, in all five borders, over random and degenerate
    rects (zero, negative, and `INT32_MIN`/`INT32_MAX` extents);
  - clips, including empty ones;
  - text from a hostile corpus at random and extreme coordinates: ESC
    and CSI sequences, C0 and C1 controls, DEL, invalid and truncated
    UTF-8, U+2028, a bidi control, ZWJ sequences, wide CJK and emoji,
    combining marks, and a grapheme over 15 bytes;
  - random styles: default, indexed, and RGB colors, with every
    attribute bit including undefined bits;
  - cell reads.

  Frame-level events come on fixed periods:
  - invalidate before begin (a full redraw);
  - the sink fails (`OVD_ERROR_IO`);
  - abandon instead of end;
  - an unchanged frame (0 bytes, no sink call);
  - misuse: begin while open, end while closed;
  - `ovd_terminal_set_size` to the current size.

  None of these may allocate.
- **Sizes, depths, and synchronized output.** Each run creates a
  terminal at size A, checks the first frame plus 100 more, resizes to
  size B, and checks the 100 frames after the resizing frame. The pairs
  are 1×1 → 200×60, 80×24 → 1×1, and 200×60 → 80×24. Each pair runs at
  all four color depths, with synchronized output both off and on. The
  size and the synchronized flag set the output bound (R30), and the
  depth selects the SGR path.
- **Non-vacuity.** Two checks keep the test from passing by accident:
  - the resizing frame must show `allocate_calls` increase, which shows
    the counter is live;
  - the sink must have received bytes during the run, including at
    least one full redraw (`CSI 2J`).
- **Benchmark clock: `timespec_get`.** It uses `TIME_MONOTONIC` when
  `<time.h>` defines it (C23 makes it optional) and
  `timespec_get(…, TIME_MONOTONIC)` succeeds. Otherwise it uses
  `TIME_UTC`. The benchmark prints which clock it used and the smallest
  nonzero step it observed between consecutive reads. The macOS SDK
  with AppleClang 21 and MinGW-w64 GCC define no `TIME_MONOTONIC`
  (observed), so those builds use `TIME_UTC`, a wall clock. If any sample is negative, the
  clock went backwards, and the run fails without reporting a number.
  *Rejected:*
  - `clock_gettime` and `QueryPerformanceCounter`: these need OS
    headers outside `src/platform/`;
  - `clock()`: it measures processor time, and its resolution and
    meaning differ by C library.
- **What is timed: `ovd_frame_end` alone, per frame (SC-008).** That
  includes the diff, output encoding, and the sink call. The sink only
  records the length. `ovd_frame_begin` and drawing are outside the
  timed region.
- **Statistics, over N measured frames per scenario:** min, median,
  mean, p99, and max, with median and p99 by nearest rank. For bytes
  per frame: min, mean, and max. N defaults to 1000, and an optional
  positive integer argument overrides it. Each scenario first runs one
  untimed setup frame.
- **Scenarios** (headless 200×60, truecolor, synchronized output off):
  - *full redraw:* the base screen, with `ovd_terminal_invalidate`
    before every frame. The base screen paints an RGB background over
    every cell, then draws:
    - a header row with its own RGB background;
    - a light box around the body;
    - body text rows in alternating RGB foregrounds;
    - a status row.
  - *one-cell change:* the base screen, with one body cell alternating
    between two glyphs each frame.
  - *scrolling region:* the base screen, with each body row showing
    log line `frame + row`, so every body row changes each frame.
  - *no change:* the base screen every frame (0 bytes).
  - *SC-003 worst case:* a 999×999 default screen. The bottom-right
    cell alternates between two styles with both colors RGB and
    three-digit components, holding `𝐀`/`𝐁` (U+1D400/U+1D401, 4 bytes,
    East Asian Width N). That gives the longest cursor move, the
    longest RGB SGR, and the longest single-code-point narrow glyph.
    The buffers at that size are about 130 MB, an INFERRED figure from
    the layouts: two grids of 30 bytes per cell, plus an output bound of
    73 bytes per cell.
- **Machine, compiler, and build info come from CMake at configure
  time,** written to a generated header in the build tree:
  - `CMAKE_C_COMPILER_ID` and `CMAKE_C_COMPILER_VERSION`;
  - `CMAKE_BUILD_TYPE` and its flags;
  - `CMAKE_SYSTEM_NAME` and `CMAKE_SYSTEM_PROCESSOR`;
  - `cmake_host_system_information`: processor description, physical
    cores, and total physical memory.

  The machine is labeled "configure host": it is the machine the
  binary was configured on. Strings are escaped for a C literal.
  *Rejected:* runtime queries, which need OS headers.
- **`bench` preset:** inherits `base`, Release, with
  `OVERDRIVE_BUILD_BENCHMARKS` on and `OVERDRIVE_BUILD_TESTS` off. It
  has a configure preset and a build preset, and no test preset,
  because the benchmark is not a test. The preset builds the
  `render_benchmark` program, which takes an optional frame count.
- **Failure handling.** The benchmark exits nonzero and prints the
  result name in these cases:
  - any create, begin, or end result other than `OVD_OK`;
  - bad arguments;
  - a sample buffer that cannot be allocated;
  - a clock step backwards.

  It reports SC-003's measured bytes against the 64-byte budget, and
  exits nonzero if the budget is exceeded, because then the spec's
  claim is false.

## Risks / Trade-offs

- [`TIME_UTC` is a wall clock and can step] → Steps backwards are
  detected and fail the run. A forward step inflates one sample's max
  and its mean, but not the median. The output names the clock.
- [The timer's resolution can be coarse next to a small frame, such as
  a one-cell change] → The observed resolution is printed, so a reader
  can tell when a number is at the resolution floor.
- [The 999×999 case needs about 130 MB] → A refused allocation is
  reported as `OVD_ERROR_ALLOCATION` and the run exits nonzero. It is
  never reported as a number.
- [Not built in CI, so the benchmark can stop compiling unnoticed] →
  The proof builds it on this PR. Wiring it into CI is OVD-01's file
  and needs an issue of its own.
