## Why

Issue #3 (OVD-02) supplies the color primitive the differential renderer
(OVD-08) needs for FR-023: colors are emitted at the terminal's color
depth, richer colors mapped to the nearest available one (R9), and style
changes are written as SGR sequences from the R7 subset, as a minimal
delta when attributes are only added and otherwise as reset plus the full
style (R8).

## What Changes

- Add the private header `src/color.h` and `src/color.c`: transformational
  functions with no state and no allocation.
  - `ovd_color_downsample`: maps an `OvdColor` to what a depth can show
    (R9; depth NONE drops colors).
  - `ovd_color_sgr_encode`: writes the SGR sequence that moves the
    terminal from one style to another at a depth into a caller buffer,
    bounded by capacity; nothing is written when it does not fit.
  - `OVD_COLOR_SGR_MAX_LENGTH`: the longest sequence the encoder emits,
    for the output bound of R30.
- Add `tests/test_color.c` and register it with one appended line.
- Append `src/color.c` to the library sources with one appended line.
- Record the choices the spec leaves open as R37 in `research.md`.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements remain in Spec Kit; this transaction
implements T010 and T011 without changing their requirements.

### Modified Capabilities

None.

## Impact

No public header changes. Only the paths owned by issue #3, the R37
decision entry, the T010/T011 checkboxes, and this record change.
