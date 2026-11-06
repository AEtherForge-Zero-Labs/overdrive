# Research: Widgets

Decision log for `specs/004-widgets`. Decisions are numbered R301
onward, so they never collide with 001's R1–R48, 002's R101 onward, or
003's R201 onward. Each carries a provenance tag (CLAUDE.md rule 4):

- **VERIFIED**: read or run in this repository while writing this file;
- **INFERRED**: follows from a VERIFIED source but is not stated in it;
- **ASSUMED**: from memory or general practice. The implementing issue
  checks it before code depends on it. An ASSUMED item that turns out
  false is a halt condition for that issue (Art. XII), not a silent
  redesign.

The spec has no clarification markers. Its Assumptions section fixes:
immediate mode with caller-owned state (owner, 2026-11-06), no layout
helpers, no focus manager, keyboard only, no wrapping at list ends, the
prompt's key set, the single-column ellipsis, and menus as plain lists.

## A. Present requirement (Art. VII)

The first consumer needs menus, prompts, lists, and tables:
`specs/003-packaging/spec.md`, Out of scope, names them as "a later
feature, driven by the same consumer" (VERIFIED). 001 research §B
records that it draws styled table cells by hand (VERIFIED). Every new
piece below names the FR it serves.

## B. Decisions

### R301 — One public header, `widgets.h`

- **Decision**: every public widget symbol goes in
  `include/overdrive/widgets.h`. `overdrive.h` includes it.
- **Rationale**: identity §4 names the module Widgets. §3.5 shows one
  header per module (VERIFIED). One header keeps the shared pieces
  (`OvdText`, `OvdOutcome`, `OvdWidgetStyle`, `OvdListState`) with the
  widgets that use them.
- **Alternatives**: one header per widget. Rejected: three headers
  sharing four types would need a fourth header for those types.

### R302 — Immediate mode, transparent caller-owned state

- **Decision**: each widget is a draw function and, for the interactive
  ones, a `handle_event` function, both taking a pointer to a
  transparent state record the application owns. Both correct the
  record on entry.
- **Rationale**: the owner chose immediate mode (spec Assumptions). The
  existing API is immediate too: a frame is a full description of the
  screen (VERIFIED, `render.h`).
- **Tension, Art. I and CLAUDE.md** ("has invariants → opaque handle")
  **vs. transparency**. The list state's invariant is
  `selected < count`, with the selection visible. `count` belongs to the
  application and changes between calls without Overdrive knowing. An
  opaque handle could not keep the invariant either: it would still
  have to be corrected on every call. It would also add an allocation,
  or a size constant, and take away the direct `selected = row` the
  application needs after inserting or deleting a row. **Resolution**:
  transparent records whose invariants are runtime-validated on every
  entry, and listed in the record's doc comment (Art. I's runtime
  path). The plan's Public API section repeats the reason (Art. XIV).

### R303 — Content through a callback per visible item

- **Decision**: a list gets item text from an `OvdItemText` callback
  plus a `void *context`. A table gets cell text and style from an
  `OvdCellText` callback. A helper, `ovd_text_array_item`, adapts a
  plain array of C strings.
- **Rationale**: FR-014 and SC-004 require asking only for visible
  items. FR-007 forbids depending on the application's types. A
  callback lets the application turn its own record into text at the
  moment it is drawn, with no array of all items built each frame. This
  is a function-pointer interface, so Art. VII needs it justified:
  FR-007, FR-014, and SC-004 are the present requirements.
- **Alternatives**: an array of `OvdText` for all items. Rejected: it
  costs O(n) per frame for the application, and SC-004 then holds only
  nominally. Offering both: rejected (Art. VII). The array helper covers
  the menu-of-literals case in one line.
- **Note**: a `const char *const[]` context must be passed as
  `(void *)entries`. The cast removes `const`. That is legal, and
  `-Wcast-qual` is not in the project's or the first consumer's flags
  (VERIFIED, `CMakeLists.txt` and the 003 spec). The helper never
  writes through it.

### R304 — Outcome set

- **Decision**: `OvdOutcome` has eight values: `IGNORED`, `UNCHANGED`,
  `MOVED`, `CHOSEN`, `CANCELLED`, `CHANGED`, `TRUNCATED`, `SUBMITTED`.
  It is an enum with a fixed `uint8_t` type (Art. III).
- **Rationale**: FR-006. `UNCHANGED` was added to the spec during
  planning. Without it, Up on the first item would read as "not used",
  and an application that acts on unused keys would act on a key the
  list consumed. The spec was amended in the same change.

### R305 — The list owns movement; the table reuses it

- **Decision**: `ovd_list_handle_event(state, count, event)` serves both
  widgets. The table has no handler of its own.
- **Rationale**: FR-023 says the table moves exactly as the list does.
  One function makes that true by construction. The count is a
  parameter, because the handler needs no other content.

### R306 — Page size comes from the last draw

- **Decision**: drawing stores the number of item rows it showed in
  `state->page`. Page Up and Page Down move by it, read as at least 1.
- **Rationale**: FR-013 ("rows visible in the last drawn rectangle").
  The handler never sees a rectangle, so the draw has to leave the
  number behind. A handler called before any draw moves by 1.

### R307 — Minimal scrolling

- **Decision**: correction sets `top = selected` when the selection is
  above the view, and `top = selected - page + 1` when it is below. It
  never moves otherwise.
- **Rationale**: FR-012 ("the smallest amount"). Spec US1 scenario 1
  (50 items, 5 rows, Down × 7 gives rows 4–8) follows from it.

### R308 — Shared text fitting, one internal module

- **Decision**: a private module, `src/textfit.{c,h}`, measures
  graphemes and fits text into a width. It cuts at a grapheme boundary,
  adds U+2026 when the width is at least 2, and leaves out a wide
  grapheme that would be cut. It aligns text that fits left or right,
  and cut text always fills from the left, because a cut value has no
  right end to align. The list, the table, and the prompt all draw
  through it.
- **Rationale**: FR-015 and FR-021 state one rule three times. One
  implementation keeps the three identical (Art. IX: reuse, don't
  duplicate). It measures with `ovd_unicode_next_grapheme`, which the
  canvas also uses (VERIFIED, `src/canvas.c:150`). So the width it
  measures is the width the canvas draws, including replaced graphemes,
  which are 1 wide.
- **Paradigm**: transformational. Text and width in, a span and a pad
  out. Nothing is retained (Art. IV).

### R309 — Clip handling

- **Decision**: each draw sets the clip to the area intersected with
  the current clip, and restores the previous clip before returning.
- **Rationale**: FR-001. `ovd_canvas_set_clip` intersects only with the
  canvas bounds, not with the current clip (VERIFIED, `render.h`), so
  the widget intersects with `ovd_canvas_clip()` itself first. The
  canvas then discards everything outside the clip, with one exception
  (VERIFIED, `render.h` Drawing, and `tests/test_canvas.c` R20): when a
  draw overwrites one half of a wide grapheme, the other half becomes a
  space in its own style, even outside the clip. A widget whose edge
  cuts a wide grapheme the application drew earlier therefore blanks
  one cell outside its area. FR-001 and SC-005 state this exception.
- **Alternatives considered**: avoid the blank by leaving an edge cell
  undrawn when it holds half of a wide grapheme. That leaves the
  widget's own area incompletely drawn, and doing it cleanly needs a
  canvas change that has no present requirement (Art. VII).

### R310 — Selected-row and cursor style combination

- **Decision**: the selected row, and the prompt's cursor cell, combine
  the cell's style with `selected` or `cursor`. The attrs are OR'ed, and
  each non-default color of the overlay replaces the cell's.
- **Rationale**: FR-009 and FR-022 together. A red "overdue" cell must
  stay red under the highlight. With the default overlay (reverse
  video, default colors) it does.
- **Alternatives**: the selected style replaces the cell style.
  Rejected: it would erase the per-cell meaning on exactly the row the
  user is looking at.

### R311 — Default appearance

- **Decision**: `normal` is the default style; `selected` and `cursor`
  add reverse video; `header` adds bold; `empty` adds dim. A nullptr
  style pointer means these defaults.
- **Rationale**: FR-009 asks for defaults readable on a monochrome
  terminal. Reverse video needs no color support. Dim may be ignored by
  some terminals (render.h: "Terminals that lack one ignore it"), and
  the empty text is still readable without it.

### R312 — Prompt storage and its invariant

- **Decision**: `OvdPromptState` points to a buffer of `capacity` bytes
  the application owns. Overdrive keeps `buffer[length] == '\0'`, so at
  most `capacity - 1` bytes are text, and the text is also a C string.
  `cursor` and `scroll` are byte offsets on grapheme boundaries.
- **Rationale**: FR-016 and FR-005 (no allocation). The NUL lets a C
  consumer pass the text on directly. Byte offsets make validation
  O(length) with no extra storage.
- **Repair**: text that the application writes into the buffer directly
  may be invalid. Before an edit, ill-formed subparts and controls are
  removed in place, because replacing a 1-byte subpart with a 3-byte
  U+FFFD could overflow a full buffer. `ovd_prompt_set_text` is the
  route that replaces with U+FFFD, as far as room allows. Drawing never
  changes the text bytes. Its only write is the terminating NUL from
  entry correction. It draws the stored bytes through the canvas's
  replacement rules. The spec's edge case was amended to say this.

### R313 — Prompt editing by grapheme

- **Decision**: boundaries are found by scanning from offset 0 with
  `ovd_unicode_next_grapheme`. Backspace removes the grapheme that ends
  at the cursor, Delete the one that starts there. Left and Right move
  one boundary. After an insertion, the cursor goes to the end of the
  grapheme that holds the last inserted byte.
- **Rationale**: FR-017 and FR-018. Segmentation restarts at each call
  of the iterator (VERIFIED, `render.h`, "Segmentation restarts with
  each call"), so boundaries are defined from the start of the text. A
  scan from 0 is the only way to match what the canvas draws. The cost
  is O(length) per key. For one line that is a non-issue, and SC-005's
  fuzz bounds it in time.
- **The iterator spans long clusters**: it advances `*offset` past
  every input byte the grapheme covers, even past the 15 bytes it
  stores (VERIFIED, `src/unicode.h`). So a cluster longer than 15 bytes
  is still edited as one unit, matching the single U+FFFD the canvas
  draws for it.
- **Inserting before an extender**: if the text after the cursor starts
  with a combining mark, an inserted base takes it over. The cursor
  rule above keeps the cursor on a boundary after that.

### R314 — Typed code points need a UTF-8 encoder

- **Decision**: add `ovd_unicode_encode_utf8` to `src/unicode.{c,h}`.
- **Rationale**: `OvdKeyEvent` carries a code point (VERIFIED,
  `input.h`). The prompt stores UTF-8. `unicode.c` already owns
  decoding (002 R118), so the inverse belongs beside it (Art. IX).
  Surrogates and values above U+10FFFF encode as nothing, and the key
  returns `OVD_OUTCOME_IGNORED`. The decoder never produces them, but
  an application can build any event.
- **Alternatives considered**: reuse the static `encode_utf8` in
  `examples/input_demo.c` (VERIFIED, read). The library cannot depend
  on an example, and an example sees only the public API, so the
  example keeps its copy.

### R315 — What the prompt filters

- **Decision**: in pasted text and `ovd_prompt_set_text`, newline (LF),
  carriage return (CR), and tab become a space, and other C0 controls,
  DEL, C1 controls, U+2028/9, and bidi controls are dropped. A typed
  control code point returns `IGNORED`. The Tab key is not text: it
  returns `IGNORED`.
- **Rationale**: FR-020 keeps the text one line. Dropping the same set
  the canvas replaces (R17 in 001) means the stored text never holds
  anything the canvas would draw as U+FFFD. Leaving Tab unused lets an
  application move between widgets with it, with no focus manager
  (spec Assumptions).
- **Paste never splits a scalar value**: "valid UTF-8, never ending
  inside a scalar value" (VERIFIED, `input.h`, `OvdPasteText`). So each
  chunk is inserted on its own. A grapheme split across chunks joins
  through R313's extender rule.

### R316 — Table widths

- **Decision**: one blank column between neighbouring columns. Fixed
  columns take `amount` cells. The width left after them and the gaps
  is split among fill columns by `amount`, rounding down, and the cells
  left over go one each to the fill columns from the left. Columns are
  laid out left to right until the width runs out, and the last one is
  cut.
- **Rationale**: FR-021 ("fixed in cells or a share of the remaining
  width"). Rounding down plus left-first leftovers is deterministic and
  uses the exact width, which the expected screens need.
- **Alternatives**: auto-sizing to content. Rejected: it would ask for
  every row's text, against FR-014. Configurable gaps: not asked for.

### R317 — A one-row table

- **Decision**: with area 1 row tall, the table draws no header, and
  the one row shows the selected row.
- **Rationale**: FR-023 says the header shows from 2 rows. With 1 row,
  showing the selection keeps FR-012 (the selection is visible).

### R318 — The ellipsis is U+2026, 1 column wide

- **Decision**: the truncation mark is U+2026, drawn 1 column wide.
- **Rationale**: spec Assumptions. U+2026's East_Asian_Width is
  Ambiguous (VERIFIED, `third_party/ucd/EastAsianWidth.txt`:
  `2024..2027 ; A`). Overdrive draws Ambiguous as 1
  (001 R16, VERIFIED in `render.h`: 2 only for W or F), so the layout is
  self-consistent. A terminal set to draw Ambiguous characters 2 wide
  would misalign. The same is true today for every Ambiguous character
  any application draws. UNVERIFIED in a real terminal until the manual
  checklist.

### R319 — Testing: canvas read back into the screen model

- **Decision**: widget tests draw on a headless terminal's canvas, read
  every cell back with `ovd_canvas_cell` into a `TestScreen`, and
  compare with `test_screen_from_rows` expectations that name their
  styles. A shared helper, `tests/support/canvas_screen.{c,h}`, does the
  read-back.
- **Rationale**: Art. VI and FR-010. The screen model and its row
  notation already exist (VERIFIED, `tests/support/screen.h`).
  `test_canvas.c` and `test_box.c` each read cells back with their own
  loops (VERIFIED). Widgets add three more tests, so the loop becomes a
  helper. The canvas is the widgets' output, and diff and output are
  already proven by 001, so going through VT replay would test nothing
  new.

### R320 — Fuzz and steady state

- **Decision**: `test_widget_fuzz.c` runs at least 10⁵ seeded random
  cases per widget, under `debug` and `sanitize`. Each case is a random
  state, area, content, and sequence of 1 to 8 events, with a draw after
  each event: about 10⁶ calls per widget. Values are drawn
  half from edge sets (0, ±1, `INT32_MIN`, `INT32_MAX`, the content's
  count, the buffer's capacity) and half uniformly. After every call it
  checks:
  - nothing was written outside the area (by comparing a canvas
    pre-filled with a sentinel that includes wide graphemes across the
    area's edges), apart from FR-001's blank half of such a grapheme;
  - the callbacks were asked only for in-range indices, and at most
    `page` of them;
  - the prompt's text is valid UTF-8 with no controls,
    `buffer[length] == 0`, and cursor and scroll are on boundaries.

  `test_steady_state.c` gains 10,000 draw-and-handle cycles per widget
  under the counting allocator, expecting 0 calls (SC-003).
- **Rationale**: SC-003 and SC-005. The counting allocator and the
  steady-state test exist (VERIFIED, `tests/support/test_alloc.h`,
  `tests/test_steady_state.c`). The count is set by time. Every call
  draws on a canvas and the presets' test timeout is 60 s (VERIFIED,
  `CMakePresets.json`), so 10⁷ cases per widget would not finish under
  the sanitizers. The spec's first draft said 10⁷. It was lowered while
  writing the tasks, and edge-weighted generation stands in for volume.
  That 10⁵ cases fit in 60 s under `sanitize` is ASSUMED. OVD-29
  measures and records the run time. If it does not fit, the test gets
  its own `TIMEOUT`, as the consumer tests did (003 R208), not a smaller
  count.

### R321 — An example, last

- **Decision**: `examples/widget_demo.c` shows a menu, a table of
  sample rows, and a prompt on a real terminal. Tab switches the active
  widget. The manual checklist gains its steps.
- **Rationale**: Art. V puts examples after the mechanism is proven,
  and repo specifics require a real-terminal observation for anything
  visible. SC-001's "10 lines per frame" is shown by the menu part of
  the example, which the docs quote.

## C. Tensions

- **Art. I and CLAUDE.md (opaque for invariants) vs. caller-owned
  transparent state**: resolved by R302.
- **Art. VII vs. a function-pointer interface**: justified by FR-007,
  FR-014, and SC-004 (R303).
- **Identity §4.4 and §4.6 vs. no Layout module**: §4.4 says Layout
  "computes widget geometry", and §4.6 builds widgets "on the
  lower-level rendering and layout interfaces". Here the caller's
  `OvdRect` is the layout interface, and the application computes it.
  The identity spec binds naming (Art. XIV) and is not a module list
  (constitution: "a naming map"), so no Layout module is added without
  a present requirement (Art. VII).
- **Spec amendments made during planning**: the `UNCHANGED` outcome
  (R304), direct-buffer repair (R312), and FR-001's wide-glyph
  exception (R309, found in review). Both are in `spec.md` in this
  change and recorded here (Art. IX).
