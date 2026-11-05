## Why

Issue #15 (OVD-14) covers tasks T039 and T040. `README.md` and
`docs/architecture.md` still say "nothing is implemented". That has been
false since OVD-01 to OVD-11 and OVD-13 merged. Art. XV makes every such
statement a claim that needs evidence, so the stale text is itself an
unbacked claim.

Two further sources of edits:
- The comment on #15 lists four architecture statements that the #20
  contract resolutions (R23, R30, R31, R33) made wrong.
- `docs/compatibility.md` (T040) does not exist.

## What Changes

- **`docs/architecture.md`**
  - Status changes from planned to implemented.
  - The module diagram is redrawn from the actual `#include` graph. It
    gains `terminal_system.c`; `box.c` draws through the canvas API.
  - The four corrections from the issue comment are applied: the resize
    step, the autowrap bullet, and the `OvdCanvas` and buffer ownership
    rows.
  - New section: R47's measured benchmark numbers, with command, machine,
    build type, and date.
- **`README.md`**
  - Status changes from "design phase" to the implemented rendering core
    (experimental 0.x).
  - The build and test commands use the presets.
  - Usage covers how to consume the library: `add_subdirectory`, because
    there are no install rules.
  - Links point to the real headers and the measured numbers.
- **New `docs/compatibility.md`**
  - Terminals actually observed, with versions. There are none so far,
    and the file says so.
  - The automated build and test platforms, with the toolchain versions
    CI logged, labeled as not terminal observations.
- **`specs/001-render-core/tasks.md`**: tick T039 and T040.

## Capabilities

### New Capabilities

None. This is a documentation change.

### Modified Capabilities

None.

## Impact

- Documentation only. The issue owns `docs/architecture.md`,
  `docs/compatibility.md`, and `README.md`.
- The T039 and T040 ticks in `specs/001-render-core/tasks.md`, as #13
  did for its tasks.
- No code, header, build, or CI change.
