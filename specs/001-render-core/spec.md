# Feature Specification: Rendering Core (minimum viable engine)

**Feature Branch**: `001-render-core`

**Created**: 2026-10-26

**Status**: Approved for implementation (constitution v1.3.0)

**Input**: "Prepare a specification for the rendering engine and its minimum
viable architecture. Define the public contracts for terminal lifecycle,
frame rendering, colors, cells, and drawing. Do not create every proposed
module immediately."

## Scope in one paragraph

An application can acquire a terminal (or a headless stand-in), describe
each frame as a full grid of styled cells through a small drawing API, and
have Overdrive write only the difference to the screen, then restore the
terminal on exit. This is the Core, Render, Canvas, and Platform modules
of `OVD-IDENTITY-001` §4, at the smallest size that does this end to end.
Layout, Motion, Widgets, and Input are **not** part of this feature
(see Out of scope).

## User Scenarios & Testing *(mandatory)*

The users are developers of C applications that link Overdrive.

### User Story 1 — Render frames headlessly and get minimal output (Priority: P1)

A developer creates a headless terminal of a fixed size with an output
sink, draws styled text and filled areas into a frame, and ends the
frame. The sink receives the bytes that would bring a blank screen to
that picture. Drawing the same picture again produces no output; changing
one cell produces output for that cell only.

**Why this priority**: It is the whole rendering mechanism, provable with
no terminal, so it can be built and regression-tested first (Art. V).
Every other story sits on it.

**Independent Test**: A test program creates a headless terminal, renders
frames, decodes the sink bytes with a test-only terminal interpreter, and
compares the decoded screen with the canvas contents.

**Acceptance Scenarios**:

1. **Given** a headless 80×24 terminal, **When** a frame draws "Every frame
   matters." in Ignition Orange (`#FF9138`) at (2, 1) and ends, **Then**
   replaying the sink output on a blank 80×24 screen yields exactly that
   text, in that color, at that position, and default cells elsewhere.
2. **Given** that frame was rendered, **When** an identical frame is
   rendered, **Then** the sink is not called.
3. **Given** that frame was rendered, **When** the next frame changes one
   cell, **Then** replaying all output so far yields the new picture, and
   the second frame's output is no larger than one cursor move, one style
   change, the glyph, and a style reset.
4. **Given** a headless terminal at depth 256, **When** a cell uses an RGB
   color, **Then** the output uses the nearest palette index and no
   24-bit sequence.
5. **Given** a headless terminal, **When** `ovd_terminal_set_size` changes
   its size and the next frame begins, **Then** the canvas has the new
   size and that frame's output is a full redraw.

---

### User Story 2 — Acquire and restore a real terminal (Priority: P1)

A developer runs a program that creates a system terminal, renders a few
frames, and destroys it. During the run the program owns the screen; after
it, the user's terminal is exactly as before: same screen contents and
scrollback, cursor visible, echo and wrapping restored.

**Why this priority**: A renderer that leaves the terminal broken is not
usable. Windows Terminal is the primary validation target
(`OVD-IDENTITY-001` §2.4).

**Independent Test**: The `hello_overdrive` example, run by hand in Windows
Terminal and in one POSIX terminal, following a written checklist.

**Acceptance Scenarios**:

1. **Given** Windows Terminal, **When** `hello_overdrive` runs and exits
   normally, **Then** it shows a truecolor branded screen during the run,
   and afterwards the prior screen contents, cursor, and typing echo are
   restored.
2. **Given** a POSIX terminal, **When** the same program runs, **Then** the
   same holds.
3. **Given** stdout redirected to a file, **When** a system terminal is
   created, **Then** creation fails with `OVD_ERROR_NOT_A_TERMINAL` and
   nothing is written to the file.
4. **Given** a running program, **When** the user resizes the window,
   **Then** the next frame has the new size and the screen shows no
   leftovers from the old size.

---

### User Story 3 — Draw boxes and panels (Priority: P2)

A developer outlines regions with light, heavy, double, rounded, or ASCII
borders, so a screen can have crisp panel boundaries
(`OVD-IDENTITY-001` §6.5) without hand-assembling box-drawing glyphs.

**Why this priority**: Every demo and the first consumer need panels, but
the engine is usable without them.

**Independent Test**: Headless test reading cells back with
`ovd_canvas_cell`.

**Acceptance Scenarios**:

1. **Given** a canvas, **When** a 10×4 light box is drawn at (0, 0),
   **Then** the corners are `┌┐└┘`, the edges `─` and `│`, and the
   interior unchanged.
2. **Given** a clip that cuts the box, **When** it is drawn, **Then** only
   cells inside the clip change.

---

### User Story 4 — Measure what a frame costs (Priority: P3)

A maintainer runs a benchmark that reports, for representative frames,
the bytes written and the time spent in `ovd_frame_end`, and a test that
counts allocations, so performance statements are backed by numbers
(`OVD-IDENTITY-001` §2.1, Art. XV).

**Why this priority**: Required before any performance claim; not
required for correctness.

**Independent Test**: Run the benchmark preset; run the allocation test.

**Acceptance Scenarios**:

1. **Given** a headless 200×60 terminal, **When** the benchmark runs its
   scenarios (full redraw, one-cell change, scrolling text region, no
   change), **Then** it prints per-scenario bytes per frame and timing
   statistics, plus the machine, compiler, and build type.
2. **Given** a counting allocator, **When** 100 frames of the same size
   are rendered after the first, **Then** the allocator is called zero
   times during them.

### Edge Cases

- Text containing ESC, other C0/C1 controls, DEL, newline, or tab: each
  is drawn as U+FFFD; no control sequence reaches the output.
- Invalid or truncated UTF-8: each maximal invalid subsequence is one
  U+FFFD.
- A wide glyph whose second half falls outside the clip or the right
  edge: drawn as a space.
- Overwriting either half of a wide glyph: its other half becomes a space.
- A combining mark with no base character (at the start of text): drawn
  as U+FFFD (research R5).
- A grapheme longer than the cell's storage: drawn as U+FFFD.
- Rectangles with zero or negative size, or entirely off-canvas: no-op.
- A 1×1 terminal; a terminal whose size reads as 0×0: begin fails with
  `OVD_ERROR_PLATFORM`, nothing is drawn.
- The bottom-right cell: drawing it must not scroll the screen.
- The sink returns false: `ovd_frame_end` returns `OVD_ERROR_IO` and the
  next frame is a full redraw.
- Allocation failure at create or resize: the call fails cleanly, nothing
  leaks, and the terminal keeps working at its old size (resize).
- `ovd_frame_begin` twice, `ovd_frame_end` without begin: `OVD_ERROR_STATE`.
- `ovd_terminal_destroy` with a frame open: abandons it, restores.
- `NO_COLOR` set: detection selects `OVD_COLOR_DEPTH_NONE`; an explicit
  depth in the config still wins.

## Requirements *(mandatory)*

### Functional Requirements

**Core**
- **FR-001**: Every fallible public function MUST report failure through
  an `OvdResult` and MUST leave its out parameters and observable state
  unchanged on failure.
- **FR-002**: The library MUST keep no process-wide mutable state; two
  terminals MUST be usable in one process.
- **FR-003**: The library MUST report its own version at run time.

**Terminal lifecycle (Platform)**
- **FR-010**: The system backend MUST work on Windows (Windows Terminal,
  Windows 10 1809+ console host) and on POSIX terminals (Linux, macOS).
- **FR-011**: Creating a system terminal MUST record every terminal mode
  it changes, and destroying it MUST restore each, in reverse order.
- **FR-012**: The headless backend MUST behave identically to the system
  backend in every respect except where its output goes and where its size
  comes from.
- **FR-013**: Color depth MUST be detected from the environment by
  default and MUST be overridable by the caller.
- **FR-014**: Terminal size MUST be re-read at the start of every frame.

**Render**
- **FR-020**: A frame MUST start as a full canvas of default cells, and the
  output for a frame MUST transform the previous screen into exactly the
  frame's contents.
- **FR-021**: A frame identical to the screen MUST produce no output.
- **FR-022**: A frame's output MUST be delivered in a single write.
- **FR-023**: Colors MUST be emitted at the terminal's color depth,
  mapping richer colors to the nearest available color.
- **FR-024**: Output MUST NOT depend on terminal autowrap or cause
  scrolling.
- **FR-025**: When the screen state is unknown (first frame, resize,
  failed write, `ovd_terminal_invalidate`), the next frame MUST be a full
  redraw.
- **FR-026**: Rendering MUST be testable without a terminal (headless
  backend).

**Canvas**
- **FR-030**: Every drawing operation MUST respect the clip rectangle and
  the canvas bounds and MUST NOT fail or allocate.
- **FR-031**: Text MUST be measured in terminal columns per grapheme using
  a pinned Unicode version's East Asian Width data.
- **FR-032**: No byte of caller text MUST reach the output as a control
  sequence.
- **FR-033**: The current frame's cells MUST be readable back.
- **FR-034**: (P2) Boxes MUST be drawable in five border styles, including
  an ASCII fallback.

**Identity**
- **FR-040**: Every exported symbol MUST begin with `ovd_`; the build MUST
  check this mechanically.
- **FR-041**: No public header MUST include an operating-system header.
- **FR-042**: The library MUST contain no type, name, or behavior specific
  to any consuming application.

### Key Entities

- **Terminal**: the acquired output device or headless stand-in; owns
  size, color depth, the screen's last known contents, and the open frame.
- **Cell**: one grapheme (or the trailing half of a wide one) and its
  style.
- **Style**: foreground color, background color, attribute set.
- **Color**: default, palette index, or 24-bit RGB.
- **Canvas**: the open frame's cell grid plus a clip rectangle.
- **Frame**: the interval between `ovd_frame_begin` and `ovd_frame_end`;
  a complete description of the screen.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For every rendering test, replaying the emitted output on a
  blank screen of the same size yields exactly the canvas contents
  (100% of test frames).
- **SC-002**: An unchanged frame writes 0 bytes.
- **SC-003**: Changing one narrow cell writes at most 64 bytes at
  truecolor depth with both colors RGB (budget: cursor move ≤ 10, SGR
  with two RGB colors ≤ 40, glyph ≤ 4, reset 4, plus margin; measured by
  the benchmark).
- **SC-004**: After the first frame at a given size, frames make 0
  allocator calls (measured by the allocation test).
- **SC-005**: The library builds with 0 warnings under GCC 14 and Clang
  with `-std=c23 -Wall -Wextra -Wpedantic -Werror`, on Linux, macOS, and
  Windows (MinGW-w64), and the headless suite passes on each.
- **SC-006**: The exported-symbol check finds 0 symbols without the
  `ovd_` prefix.
- **SC-007**: `hello_overdrive` passes its manual checklist in Windows
  Terminal and one POSIX terminal, with versions recorded.
- **SC-008**: Time in `ovd_frame_end` for a full 200×60 redraw is reported
  by the benchmark; a target is set from the first measurement, not
  before (Art. XV).

## Assumptions

- Consumers are single-threaded with respect to one terminal.
- Terminals targeted support ECMA-48 cursor positioning and SGR, the
  alternate screen, and DEC private modes 25 (cursor) and 7 (autowrap).
- Windows support starts at Windows 10 1809 (virtual-terminal output and
  UTF-8 console code page).
- A cell stores a grapheme of at most 15 UTF-8 bytes; longer clusters are
  rare and drawn as U+FFFD.
- Grapheme segmentation in this feature is base character plus following
  combining marks, ZWJ sequences, and variation selectors; full UAX #29
  segmentation is out of scope.

## Out of scope

- **Input** (keys, mouse, paste, focus) and raw input mode. The first
  consumer needs it, so it is the next feature (002), not this one.
- **Abnormal-exit restoration** (signals, Ctrl+C, crashes). Owned by
  feature 002, because Ctrl+C handling belongs with input handling.
  Until then, consumers restore through `ovd_terminal_destroy` on their
  normal exit paths.
- **Layout, Motion, Widgets.** Specified when a consumer requires them.
- Synchronized-output detection, terminal capability queries, images and
  graphics protocols, a stable ABI, packaging and installation.
- Integration into any consuming application.

## Dependencies on decisions outside this spec

- Constitution ratification (done: v1.3.0).
- GitHub repository creation under AEtherForge Zero Labs.
- License choice (blocks publication only, not implementation).
