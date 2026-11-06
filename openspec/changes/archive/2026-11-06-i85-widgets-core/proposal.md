## Why

Feature 004 (`specs/004-widgets/`) adds immediate-mode widgets. Every widget draws through the same text fitting, clip handling, and style combination. Their tests read the canvas back into the screen model. This change lands those shared pieces and freezes the public contract, so the list, the prompt, and the table can follow in parallel (OVD-25, tasks T301–T309).

## What Changes

- `include/overdrive/widgets.h`, copied unchanged from the contract, and included from `overdrive.h`. It is added to `overdrive_public_headers` and to 003's install-layout header row.
- `ovd_unicode_encode_utf8` in `src/unicode.{c,h}` (R314).
- `src/textfit.{c,h}`: measuring and drawing text into a width (R308, R318).
- `src/widget.{c,h}`:
  - `ovd_widget_style_default` and `ovd_text_array_item`;
  - the internal clip helpers (R309);
  - style combination (R310).
- Test support `tests/support/canvas_screen.{c,h}` (R319).
- Tests: `test_textfit` and `test_widget` (new); `test_unicode` and `test_headers` (extended).

The list, table, and prompt functions are declared but not defined. Nothing calls them until their own issues (plan.md: no stubs).

## Capabilities

### New Capabilities

None. The behavior is specified in `specs/004-widgets/`.

### Modified Capabilities

None.

## Impact

- `include/overdrive/` gains `widgets.h` and is frozen after this.
- `CMakeLists.txt` gains the header-list entry and two source lines.
- `tests/CMakeLists.txt` gains two tests.
- No change to terminal modes, allocation, or existing behavior.
