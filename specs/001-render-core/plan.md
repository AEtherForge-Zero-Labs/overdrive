# Implementation Plan: Rendering Core

**Branch**: `001-render-core` | **Date**: 2026-10-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-render-core/spec.md`

## Summary

Build the smallest Overdrive that renders end to end: a terminal (system
or headless), an immediate-mode cell canvas with clipping and
Unicode-aware text, and a differential renderer that writes only what
changed in one write per frame. Bottom-up and headless-first (Art. V):
primitives → buffer/canvas → diff → terminal/frame API → platform wiring
→ examples and benchmark. Decisions and their alternatives are in
[research.md](research.md); the public contract is the headers in
[contracts/](contracts/include/overdrive/).

## Technical Context

**Language/Version**: ISO C23 (`CMAKE_C_STANDARD 23`, extensions off)
**Primary Dependencies**: C standard library; OS console APIs (Win32
console, POSIX termios/ioctl). Vendored Unicode Character Database data
files (data, not code).
**Storage**: N/A
**Testing**: CTest + plain C test programs; headless replay interpreter
**Target Platform**: Windows 10 1809+ (Windows Terminal primary), Linux,
macOS
**Project Type**: library (static and shared), plus examples
**Performance Goals**: SC-002/003/004 (bytes and allocations; exact).
Time targets set after first measurement (SC-008, Art. XV).
**Constraints**: no allocation while drawing or in steady-state frames;
one write per frame; no process-wide state
**Scale/Scope**: ~2.5–3.5k lines of C including tests

## Constitution Check

*Gate against constitution v1.2.0 (PROPOSED). Re-checked after design.*

| Article | Gate | Status |
|---|---|---|
| I Contracts are types | Every boundary has a contract form | PASS — public: headers in `contracts/`; internal: private headers listed below; runtime invariants named in doc comments (clip, wide-glyph pairing, control-char replacement, allocation checks); external: R7 protocol subset, R5 UCD |
| II Generated code separate | Generated files never committed | PASS — width tables generated into the build tree by `cmake/gen_unicode_tables.cmake` |
| III Strict typing | Closed sets are fixed-type enums | PASS with two named tensions (attr sets, coordinates) — research §A |
| IV Ownership | Paradigm per module | PASS — table below |
| V Mechanic before interface | Order headless → system → examples | PASS — workstream order |
| VI Human-readable | Test expectations readable | PASS — expected screens as text rows; replay decodes bytes |
| VII No speculation | Each new piece justified | PASS — table below; 4 of 8 identity modules deferred |
| VIII Clean checkout | Presets, declared tools | PASS — WS0 delivers presets + baseline |
| XIII Security | Caller text never becomes control sequences | PASS — FR-032, R6, dedicated tests |
| XIV Identity/API | Every public symbol listed | PASS — table below; prefix check in CI |
| XV Claims | No unmeasured claims | PASS — README states design phase; targets labeled |
| XVI Process | Issues, PRs, human merge | PASS as drafted; see research §D.2 |

## Public API (Art. XIV plan gate)

| Header | Symbols |
|---|---|
| `core.h` | `OVD_API`, `OVD_VERSION_{MAJOR,MINOR,PATCH}`, `OvdVersion`, `ovd_version`, `OvdResult` + `OVD_OK`/`OVD_ERROR_*`, `ovd_result_name`, `OvdSize`, `OvdRect` |
| `terminal.h` | `OvdTerminal`, `OvdColorDepth`, `OvdBackend`, `OvdSink`, `OvdAllocator`, `OvdTerminalConfig`, `ovd_terminal_config_default`, `ovd_terminal_create`, `ovd_terminal_destroy`, `ovd_terminal_size`, `ovd_terminal_color_depth`, `ovd_terminal_set_size`, `ovd_terminal_invalidate` |
| `render.h` | `OvdColorKind`, `OvdColor`, `OVD_COLOR_DEFAULT`, `ovd_color_rgb`/`_hex`/`_indexed` (static inline), `OvdAttr`, `OvdStyle`, `OVD_STYLE_DEFAULT`, `OvdCellView`, `OvdCanvas`, `ovd_frame_begin`, `ovd_frame_end`, `ovd_frame_abandon`, `ovd_canvas_size`, `ovd_canvas_clip`, `ovd_canvas_set_clip`, `ovd_canvas_fill`, `ovd_canvas_text`, `ovd_canvas_cell`, `OvdBorder`, `ovd_canvas_box` |
| `overdrive.h` | umbrella only |

Transparent structs and why: `OvdColor`, `OvdStyle` — copied per cell,
must be stack values; `OvdSize`, `OvdRect`, `OvdVersion` — plain values;
`OvdCellView` — read-only view, avoids N accessor calls per cell;
`OvdTerminalConfig` — designated-field configuration, always obtained from
`ovd_terminal_config_default()` so added fields get defaults;
`OvdAllocator` — a table of callbacks by nature.

## Modules (Art. IV / VII plan gates)

| File | Paradigm | Present requirement |
|---|---|---|
| `src/core.c` | transformational | FR-001, FR-003 (result names, version) |
| `src/color.c` + `src/color.h` | transformational | FR-023 (downsampling, SGR encoding) |
| `src/unicode.c` + `src/unicode.h` | transformational | FR-031, FR-032 (decode, segment, width, sanitize) |
| `cmake/gen_unicode_tables.cmake` | generator | FR-031 without committing generated code (Art. II) |
| `src/buffer.c` + `src/buffer.h` | owning module (`OvdBuffer`: cell grid) | FR-020, FR-030 invariants (wide-glyph pairing) |
| `src/canvas.c` | owning module (`OvdCanvas`: buffer view + clip) | FR-030–FR-034 |
| `src/output.c` + `src/output.h` | owning module (`OvdOutput`: growable byte buffer) | FR-022 |
| `src/diff.c` + `src/diff.h` | transformational (screen, frame → output) | FR-020, FR-021, FR-024, FR-025 |
| `src/terminal.c` + `src/internal.h` | owning module (`OvdTerminal`) | FR-002, FR-012, FR-014, frame state machine |
| `src/terminal_system.c` | part of the terminal module: system backend | FR-010, FR-011 |
| `src/detect.c` + `src/detect.h` | transformational | FR-013 (R10) |
| `src/box.c` | transformational over the canvas | FR-034 |
| `src/platform/platform.h`, `platform_posix.c`, `platform_windows.c` | transformational OS wrappers | FR-010, FR-011 |
| `src/alloc.c` + `src/alloc.h` | transformational | R4 allocator hook |
| `tests/support/vt_replay.c` + `.h` | test-only owning module | SC-001 |
| `cmake/check_exports.cmake` | check script | FR-040 |
| `examples/hello_overdrive.c` | example | US2 |
| `bench/render_benchmark.c` | benchmark | US4 |

Deferred (no present requirement): Layout, Motion, Widgets, Input, any
off-screen canvas, capability queries, Sixel.

## Project Structure

### Documentation (this feature)

```text
specs/001-render-core/
├── spec.md
├── research.md
├── plan.md                 # this file
├── contracts/include/overdrive/{overdrive,core,terminal,render}.h
├── tasks.md
├── workgraph.json          # machine-readable issue graph for agents
└── checklists/identity-review.md
```

### Source Code (repository root)

```text
CMakeLists.txt
CMakePresets.json
cmake/  gen_unicode_tables.cmake  check_exports.cmake  overdriveConfig.cmake.in
include/overdrive/  overdrive.h  core.h  terminal.h  render.h
src/  core.c  alloc.{c,h}  color.{c,h}  unicode.{c,h}  buffer.{c,h}  canvas.c
      output.{c,h}  diff.{c,h}  terminal.c  terminal_system.c  internal.h
      detect.{c,h}  box.c
src/platform/  platform.h  platform_posix.c  platform_windows.c
third_party/ucd/  EastAsianWidth.txt  DerivedGeneralCategory.txt  LICENSE
tests/  test_core.c  test_color.c  test_unicode.c  test_canvas.c  test_diff.c
        test_terminal.c  test_alloc.c  test_box.c
tests/support/  vt_replay.{c,h}  screen.{c,h}  test_alloc.{c,h}
examples/  hello_overdrive.c
bench/  render_benchmark.c
.github/workflows/  ci.yml
```

**Structure Decision**: single library project; layout as in `AGENTS.md`.

## Workstreams

The issue graph, file ownership, and parallel lanes are in
[`docs/plan/workstreams.md`](../../docs/plan/workstreams.md) and, for
agents, [workgraph.json](workgraph.json). Tasks are in [tasks.md](tasks.md).

## Complexity Tracking

| Item | Why needed | Simpler alternative rejected because |
|---|---|---|
| Function pointers (`OvdSink`, `OvdAllocator`) | Headless testing (FR-026) and SC-004 / allocation-failure tests on all platforms | Internal-only test seam: consumers couldn't snapshot-test; `--wrap`: GNU ld only |
| Build-time table generator | FR-031 with Art. II | Committed generated table violates Art. II; hand table is unreviewable |
