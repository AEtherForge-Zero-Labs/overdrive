## Why

Every rendering test (SC-001) decides correctness by replaying the
library's terminal output on a model screen. Issue #6 (OVD-05, tasks
T018 and T019) supplies that oracle before the differential renderer
(OVD-08) exists, so the renderer is checked by something it does not
share code with.

## What Changes

- `tests/support/screen.{c,h}`: a test-only screen model (cells with
  grapheme text, width, `OvdStyle`, and an indeterminate flag), built
  either by the replay interpreter or from readable text rows plus
  style-key rows (Art. VI), with comparison and a readable dump.
- `tests/support/vt_replay.{c,h}`: a strict interpreter of exactly the
  R7 subset plus the R34 synchronized-output markers. Anything else, and
  any glyph printed in the pending-wrap state (R33), fails with a message
  naming the byte offset and the offending bytes.
- `tests/test_vt_replay.c`: the interpreter's own tests.
- `tests/CMakeLists.txt`: append-only lines for the support library and
  the test.
- `specs/001-render-core/research.md`: R37, the oracle's decisions where
  R7, R33, and R34 are silent.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

Test-only code; nothing is added to the library or its public headers.
OVD-08 must emit output this oracle accepts (R37).
