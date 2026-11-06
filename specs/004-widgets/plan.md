# Implementation Plan: Widgets

**Branch**: `004-widgets` | **Date**: 2026-11-06 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/004-widgets/spec.md`

## Summary

Add three immediate-mode widgets: a list (and menu), a table, and a
one-line prompt. Each draws into a caller-chosen rectangle of the frame
canvas and corrects a small, transparent state record that the
application owns. The list's event handler serves the table too. One
private text-fitting module gives all three the same cutting, ellipsis,
wide-glyph padding, and alignment. Nothing allocates, nothing fails, and
every behavior is proven on the headless canvas with readable expected
screens before the example touches a real terminal (Art. V). Decisions
are in [research.md](research.md) (R301–R321). The public contract is
[contracts/include/overdrive/widgets.h](contracts/include/overdrive/widgets.h).
It compiles under `-std=c23 -Wall -Wextra -Wpedantic -Werror` (Apple
Clang, checked while writing this plan).

## Technical Context

**Language/Version**: ISO C23, as 001–003
**Primary Dependencies**: none new. Builds on the canvas
(`render.h`), events (`input.h`), and the internal Unicode iterator
(`src/unicode.h`).
**Storage**: N/A. All widget state is caller-owned.
**Testing**: CTest. Headless canvas tests compare against
`TestScreen` expectations (R319), a seeded fuzz under `sanitize`
(R320), and `test_steady_state` extended. Plus a manual checklist for
the example.
**Target Platform**: as 001: Linux, macOS, Windows (Windows Terminal
primary), and the Wine cross build in CI
**Project Type**: library plus one example
**Performance Goals**: SC-003 (0 allocations) and SC-004 (only visible
rows requested). No timing claim (Art. XV).
**Constraints**: no allocation, no failure, no process state; every
input untrusted; each draw clipped to its area
**Scale/Scope**: about 1.5–2.5k lines of C, including tests

## Constitution Check

*Gate against constitution v1.4.2. Checked before research and again
after the design below. Result: PASS, with two tensions resolved.*

| Article | Gate | Status |
|---|---|---|
| I Contracts are types | Every boundary has a contract form | PASS — public types and closed enums in `widgets.h`. Runtime-validated: `OvdListState` (selection in range and visible) and `OvdPromptState` (length, NUL, UTF-8 validity, boundary offsets), each invariant written in the record's doc comment (R302, R312). |
| II Generated code separate | — | PASS — nothing generated; reuses the existing generated Unicode tables |
| III Strict typing | Closed sets are fixed-type enums | PASS — `OvdOutcome`, `OvdAlign`, `OvdColumnSizing` (`uint8_t`) |
| IV Ownership | Paradigm per module | PASS — table below |
| V Mechanic before interface | Order | PASS — text fitting, then widgets on the headless canvas, then fuzz and steady state, then the example |
| VI Human-readable | Readable expectations | PASS — expected screens as text rows plus style keys (R319) |
| VII No speculation | Each piece justified | PASS — table below; two callback types justified by FR-007, FR-014, SC-004 (R303) |
| VIII Clean checkout | Declared tools | PASS — no new tool or preset |
| IX Agent discipline | Assumptions recorded | PASS — two spec amendments recorded (R304, R312) |
| X Evidence | Unverifiable areas named | PASS — real-terminal rendering, including the ellipsis width (R318), is UNVERIFIED until the checklist |
| XI Check integrity | No weakened checks | PASS — tests added only |
| XIII Security | Untrusted text | PASS — FR-008, prompt filtering (R315), SC-005 fuzz under sanitizers |
| XIV Identity/API | Every public symbol listed | PASS — table below |
| XV Claims | No unmeasured claims | PASS — SC-003 and SC-004 are counted by tests |
| XVI Process | Issues, gated merge | PASS — `tasks.md` → issues after owner approval |

**Tensions resolved:**

- CLAUDE.md "invariants → opaque handle" vs. caller-owned transparent
  state: the invariant depends on a count only the caller knows, so an
  opaque handle would still need correction on every call, plus an
  allocation (R302).
- Art. VII vs. a function-pointer interface: required by FR-014 and
  SC-004 (only visible items) under FR-007 (no application types)
  (R303).

## Public API (Art. XIV plan gate)

| Header | Symbols |
|---|---|
| `widgets.h` (new) | `OvdText`, `ovd_text` (static inline), `OvdOutcome` + `OVD_OUTCOME_{IGNORED,UNCHANGED,MOVED,CHOSEN,CANCELLED,CHANGED,TRUNCATED,SUBMITTED}`, `OvdWidgetStyle`, `ovd_widget_style_default`, `OvdListState`, `OvdItemText`, `ovd_text_array_item`, `OvdList`, `ovd_list_draw`, `ovd_list_handle_event`, `OvdAlign` + `OVD_ALIGN_{LEFT,RIGHT}`, `OvdColumnSizing` + `OVD_COLUMN_{FIXED,FILL}`, `OvdColumn`, `OvdCellText`, `OvdTable`, `ovd_table_draw`, `OvdPromptState`, `ovd_prompt_set_text`, `ovd_prompt_draw`, `ovd_prompt_handle_event` |
| `overdrive.h` (changed) | includes `widgets.h` |

Function names carry their object (`ovd_list_handle_event`, not
`ovd_handle`), as identity §3.4 asks.

**Transparent structs, and why** (Art. XIV):

- `OvdListState` and `OvdPromptState`: caller-owned state whose
  invariant depends on caller data, corrected on entry. The caller must
  write `selected`, and read `buffer` and `length` (R302, R312).
- `OvdText`: a (pointer, length) view returned per item, like
  `OvdPasteText`.
- `OvdWidgetStyle` and `OvdColumn`: plain descriptions with no
  invariant beyond their fields, filled in field by field.
- `OvdList` and `OvdTable`: per-call descriptors. They are read, never
  stored.

## Modules (Art. IV / VII plan gates)

| File | Paradigm | Present requirement |
|---|---|---|
| `include/overdrive/widgets.h` | public header | FR-001–FR-023 (identity §4.6) |
| `src/textfit.c` + `src/textfit.h` | transformational: text and width in, span and padding out | FR-015, FR-021, FR-018; one rule for three widgets (R308) |
| `src/widget.c` + `src/widget.h` | transformational helpers shared by the widgets: clip intersection and restore (R309), style combination (R310), `ovd_widget_style_default`, `ovd_text_array_item`. The private header declares the two internal helpers | FR-001, FR-009, SC-001 |
| `src/list.c` + `src/list.h` | transformational over a caller-held record: list correction, draw, and `ovd_list_handle_event`. The private header declares the correction, `ovd_list_correct`, which the table also calls (R305) | FR-011–FR-015, FR-023 |
| `src/table.c` | transformational: column layout (R316) and table draw | FR-021–FR-023 |
| `src/prompt.c` | transformational over a caller-held record: repair, editing, draw | FR-016–FR-020 |
| `src/unicode.c`, `src/unicode.h` (changed) | transformational: adds `ovd_unicode_encode_utf8` | typed code points into the prompt (R314) |
| `tests/support/canvas_screen.{c,h}` | test support: reads a canvas back into a `TestScreen` | FR-010, SC-002 (R319) |
| `tests/test_textfit.c`, `test_widget.c`, `test_list.c`, `test_table.c`, `test_prompt.c`, `test_widget_fuzz.c`; `test_steady_state.c`, `test_unicode.c`, and `test_headers.c` extended | tests | SC-002–SC-005 |
| `examples/widget_demo.c`, `docs/validation/manual-checklist.md` | example and checklist | Art. V, repo specifics, SC-001 |

No module retains state between calls. "Over a caller-held record"
means the functions read and correct a record the caller passes in.
The record's owner is the application (FR-002), so no Overdrive module
owns state (Art. IV).

**Deferred (no present requirement):** layout helpers, focus
management, mouse, multi-line editing, menu shortcut letters,
configurable gaps and truncation marks, auto-sized columns, wrapping
selection, and word-wise editing.

## Terminal modes

Not applicable. No change to `ovd_terminal_create` or
`ovd_terminal_destroy`, and widgets write only through the canvas.

## Project Structure

### Documentation (this feature)

```text
specs/004-widgets/
├── spec.md
├── research.md
├── plan.md                 # this file
├── data-model.md
├── quickstart.md
├── contracts/include/overdrive/widgets.h
├── checklists/requirements.md
├── tasks.md                # speckit-tasks
└── workgraph.json          # speckit-tasks: issue graph (OVD-25 onward)
```

### Source Code (repository root)

```text
include/overdrive/  widgets.h (new)  overdrive.h
src/  textfit.{c,h}  widget.{c,h}  list.{c,h}  table.c  prompt.c (new)  unicode.{c,h}
tests/  test_textfit.c  test_widget.c  test_list.c  test_table.c  test_prompt.c
        test_widget_fuzz.c (new)
        test_steady_state.c  test_unicode.c  test_headers.c (extended)
tests/support/  canvas_screen.{c,h} (new)
examples/  widget_demo.c (new)
docs/validation/  manual-checklist.md (extended)
docs/  architecture.md  (widgets section; "Not in this architecture" updated)
```

**Structure Decision**: single library project, laid out as in
`AGENTS.md`. No new directory. `CMakeLists.txt` and
`tests/CMakeLists.txt` stay shared append-only.

## Workstreams (for speckit-tasks)

Six issues, OVD-25 to OVD-30, continuing 003's OVD-22–24. Two depend
on 003's issues, because those issues own files these need (Art. IX):
OVD-25 starts after OVD-23 (`CMakeLists.txt`, `tests/CMakeLists.txt`),
and OVD-30 after OVD-24 (`README.md`, `docs/architecture.md`).

1. **OVD-25 Contract freeze, shared helpers, text fitting.**
   - Adds `widgets.h` to `include/`, with its line in
     `overdrive_public_headers`, and includes it from `overdrive.h`.
   - Adds `src/widget.{c,h}`: the style default, the array helper, and
     the clip and combine helpers.
   - Also adds `textfit.{c,h}`, `unicode.c`'s encoder, the
     `canvas_screen` support, and their tests.

   After this, `include/overdrive/` is frozen; changing it takes a
   contract-change issue.
2. **OVD-26 List.** `src/list.{c,h}` and `test_list.c`. After 1.
3. **OVD-27 Prompt.** `src/prompt.c` and `test_prompt.c`. After 1, in
   parallel with 2, since the files are disjoint.
4. **OVD-28 Table.** `src/table.c` and `test_table.c`. After 2: it
   reuses the list's correction and handler.
5. **OVD-29 Fuzz and steady state.** `test_widget_fuzz.c` and
   `test_steady_state.c`. After 2, 3, and 4.
6. **OVD-30 Example, checklist, docs.** `examples/widget_demo.c`, the
   manual checklist, and `docs/architecture.md`. After 5. Real-terminal
   steps are reported as UNVERIFIED with a `needs-human` follow-up.

There are no stubs. A function declared in `widgets.h` and defined by a
later issue breaks no build in between, because nothing calls it until
that issue's own test does. The export check lists only defined
symbols. Each widget issue adds its own source file to `CMakeLists.txt`.

## Complexity Tracking

| Item | Why needed | Simpler alternative rejected because |
|---|---|---|
| Two callback types (`OvdItemText`, `OvdCellText`) | FR-014 and SC-004 under FR-007 | An all-items array costs O(n) per frame and copies the app's data into text each frame (R303) |
| Transparent state with entry correction | R302 | An opaque handle needs an allocation and still needs correction, because the count is the caller's |
| Shared text-fitting module | FR-015 and FR-021 stated three times | Three copies of the cutting rules would drift |
