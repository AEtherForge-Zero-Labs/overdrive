# Tasks: 004-widgets

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [quickstart.md](quickstart.md),
[contracts/](contracts/include/overdrive/widgets.h)

Format: `- [ ] T### [P] [Story] Description naming the file (depends on …)`.

- **Numbering.** Task ids start at T301 and decisions at R301, so
  neither collides with 001–003.
- **Parallel.** `[P]` marks a task that is parallel-safe with the other
  `[P]` tasks in its phase (disjoint files).
- **Issues.** Each phase maps to one GitHub issue (`OVD-NN`, continuing
  003's OVD-22–24; see [workgraph.json](workgraph.json)). Once the
  issues exist, lines gain `→ #<number>`.
- **Tests first.** Every behavior task comes after its test task, which
  must be seen failing first (`AGENTS.md`, Definition of done). Record
  the failure for the PR.
- **Assumptions.** A task that depends on an ASSUMED item in
  `research.md` first checks it and quotes the evidence in the PR. A
  false assumption halts the issue (Art. XII).
- **Test style.** Widget tests use the public API on a headless
  terminal. They compare the canvas with expected screens written as
  text rows plus style-key rows (Art. VI, R319). A test that compiles an
  internal module in does so as `test_unicode` does in
  `tests/CMakeLists.txt`, because shared builds hide internal symbols.
- **Symbols.** Every non-static symbol carries `ovd_`, hidden or not
  (`AGENTS.md`, research R12 of 001). Internal helpers are declared in
  a private header under `src/`, without `OVD_API`.
- **Shared files.** `CMakeLists.txt` and `tests/CMakeLists.txt` are
  shared append-only inside this feature: append your own lines and
  rebase on `main`. 003's OVD-22 and OVD-23 own both files outright, so
  OVD-25 starts after OVD-23 merges. 003's OVD-24 owns `README.md` and
  `docs/architecture.md`, so OVD-30 starts after OVD-24 merges (Art. IX;
  CLAUDE.md, Parallel agents).

## Phase 1 — OVD-25 Contract freeze, shared helpers, text fitting (depends on 003's OVD-23; blocks everything else)

Goal: the public header is frozen, and the pieces every widget draws
through are proven: text fitting, the encoder, the style helpers, and
the canvas read-back for tests. Independent test:
`ctest --preset debug -R '^test_(headers|unicode|textfit|widget)$'`.

- [ ] T301 Copy `specs/004-widgets/contracts/include/overdrive/widgets.h` into `include/overdrive/widgets.h` unchanged (the contract freeze point for 004). Add `widgets.h` to the header row of "Install layout" in `specs/003-packaging/contracts/consumer.md`, since that row lists the installed headers exactly (003 FR-010).
  - Add `#include <overdrive/widgets.h>` to `include/overdrive/overdrive.h` after `input.h`, and change its `PROPOSED CONTRACT` line to name 004.
  - Add `widgets.h` to `overdrive_public_headers` in `CMakeLists.txt`. This issue owns that one line. `tests/CMakeLists.txt` then compiles the header alone and twice, and checks that it includes only public and ISO C headers.
  - Extend `tests/test_headers.c`:
    - `static_assert` that `OvdOutcome`, `OvdAlign`, and `OvdColumnSizing` are each 1 byte (Art. III);
    - at run time, `ovd_text("abc").length == 3`, and `ovd_text(nullptr)` gives `{ nullptr, 0 }`.
- [ ] T302 [P] Write `tests/support/canvas_screen.h` and `tests/support/canvas_screen.c` (R319):
  - `canvas_screen_open(cols, rows, &fixture)` creates a headless terminal of that size with a discarding sink, and begins a frame. `canvas_screen_close(&fixture)` abandons the frame and destroys the terminal.
  - `canvas_screen_capture(canvas)` reads every cell with `ovd_canvas_cell` into a `TestScreen`. A lead cell goes through `test_screen_put`, and a width-0 continuation is skipped.
  - `canvas_screen_expect(canvas, text_rows, style_rows, legend, legend_count)` builds the expected screen with `test_screen_from_rows`, compares it with `test_screen_equal`, and on a mismatch prints both dumps and the first difference.
  - `canvas_screen_classify` is a `TestClassify` for the code points the widget tests use, each checked against `third_party/ucd/EastAsianWidth.txt` and `DerivedGeneralCategory.txt`. It does not call `src/unicode` (001 R38: the oracle must not share the module it checks).
  - Tests link `overdrive_vt_replay` for `support/screen.c` and add `support/canvas_screen.c` with `target_sources`.
- [ ] T303 [P] Add the encoder cases to `tests/test_unicode.c` (R314). `ovd_unicode_encode_utf8` writes:
  - 1 byte for U+0000 and U+007F;
  - 2 for U+0080 and U+07FF;
  - 3 for U+0800 and U+FFFF;
  - 4 for U+10000 and U+10FFFF;
  - 0 for U+D800, U+DFFF, and 0x110000.

  Every written sequence decodes back through `ovd_unicode_decode_utf8` to the same scalar value with `OVD_UTF8_SCALAR`.
- [ ] T304 [P] Write `tests/test_textfit.c`, compiling `src/textfit.c` and `src/unicode.c` in, with `add_dependencies(... ovd_unicode_tables)` and the generated include directory, as `test_unicode` has. Table-driven cases from data-model.md, "Internal: text fitting":
  - text that fits, aligned left and right, with the padding on the correct side;
  - `abcdef` in w = 4 gives `abc…`; `ab中` in w = 3 gives `ab…`; `a中b` in w = 3 gives `a… ` (the wide grapheme left out, pad 1);
  - cut text fills from the left even when aligned right;
  - w = 1 with text that does not fit draws the first grapheme when it is 1 wide, and a space when it is 2 wide; w = 0 draws nothing; a negative w reads as 0;
  - `e` + U+0301 is one grapheme of width 1;
  - a 16-byte cluster is one grapheme of width 1, drawn as U+FFFD;
  - each ill-formed subpart is 1 wide, matching `ovd_canvas_text`;
  - empty text, and a `nullptr` with a nonzero length, are empty.

  Also check that the width the fitter measures equals the column count `ovd_canvas_text` returns for every case string. Measuring with the same iterator as the canvas is the point of R308.
- [ ] T305 [P] Write `tests/test_widget.c`, public API only:
  - `ovd_widget_style_default()` gives `normal` = `OVD_STYLE_DEFAULT`; `selected` and `cursor` with `OVD_ATTR_REVERSE` and default colors; `header` with `OVD_ATTR_BOLD`; `empty` with `OVD_ATTR_DIM` (R311).
  - `ovd_text_array_item` on a three-string array returns each string, and on a `nullptr` context returns empty text.
  - A self-check of T302: a frame with `ovd_canvas_text` of `ab中` in a style, captured and compared with `canvas_screen_expect`, passes. The same frame compared with a wrong expectation fails, and the message names the cell.

  (Depends on T302.)
- [ ] T306 Implement `ovd_unicode_encode_utf8(uint32_t code_point, char out[4])`, returning the byte count, 0 to 4, in `src/unicode.h` and `src/unicode.c` (R314). (Depends on T303.)
- [ ] T307 Implement `src/textfit.h` and `src/textfit.c` (R308): a pure measuring function that returns the span, width, mark flag, and padding for text, width, and alignment, and a drawing function that fills exactly `width` cells at `(x, y)` in a style through `ovd_canvas_text`. Append `target_sources(overdrive PRIVATE src/textfit.c)` to `CMakeLists.txt`. (Depends on T304.)
- [ ] T308 Implement `src/widget.h` and `src/widget.c`:
  - `ovd_widget_style_default` and `ovd_text_array_item`;
  - the internal clip helpers `ovd_widget_clip_enter` and `ovd_widget_clip_leave`, which intersect the area with `ovd_canvas_clip()`, set it, and restore the previous clip (R309);
  - `ovd_widget_style_combine`, which ORs the attrs and lets the overlay's non-default colors replace (R310).

  Append `src/widget.c` to `CMakeLists.txt`. (Depends on T305.)
- [ ] T309 Proof: `debug`, `shared`, `sanitize`, and `windows-cross-debug` from clean, with 0 warnings and every test passing, including `consumer_*` if 003's OVD-22 has merged. That test compiles the umbrella header, now with `widgets.h`, under the first consumer's flags. (Depends on T301–T308.)

## Phase 2 — OVD-26 List (US1, P1; depends on OVD-25)

Goal: pick from a list or a menu (US1). Independent test:
`ctest --preset debug -R '^test_list$'`.

- [ ] T310 [US1] Write `tests/test_list.c`, with expected screens through `canvas_screen` and the style key `S` for the default selected style:
  - **US1 scenarios 1–6** as written in the spec. Items are `item 1` to `item 50` from a callback, in a 20 × 5 area. Scenario 1 gives `selected` 7 and `top` 3, showing items 4 to 8. Scenario 2's Page Down gives `selected` 12 and `top` 8, and End gives `selected` 49 and `top` 45.
  - **Correction** (data-model.md), each after one draw and separately after one handled key:
    - `selected` −5 → 0; 1000 → n − 1;
    - `top` past the selection → moved to show it;
    - `page` 0 → 1;
    - n = 0 → `selected` −1 and `top` 0;
    - a shrunken area still shows the selection (spec Edge Cases).
  - **Outcomes** (FR-006):
    - Up on the first item and Down on the last give `UNCHANGED`;
    - Enter gives `CHOSEN` with `selected` unchanged;
    - Escape gives `CANCELLED`;
    - F5, a letter, Ctrl+Down, Alt+Down, a paste, a resize, and a mouse event give `IGNORED`, with the state byte-for-byte unchanged apart from correction;
    - with n ≤ 0, everything gives `IGNORED`;
    - a handler called before any draw pages by 1 (R306).
  - **FR-001**:
    - nothing is drawn outside the area (the canvas is pre-filled with a sentinel);
    - a wide grapheme straddling each edge of the area: the outside half becomes a space in its own style, and nothing else outside changes (FR-001's exception, R309);
    - a caller clip narrower than the area limits the drawing, and `ovd_canvas_clip` afterwards equals the clip before;
    - an area partly off the canvas works; zero or negative areas draw nothing;
    - 1 × N and N × 1 areas work.
  - **FR-002**: two lists with separate states drawn in one frame do not affect each other.
  - **FR-004**: `nullptr` canvas, state, list, or style (the last means defaults) do nothing and do not crash. A `nullptr` callback is an empty list.
  - **Text**: an item wider than the area ends with `…`; a wide grapheme at the edge is left out and padded; item text with ESC, NUL, and ill-formed UTF-8 shows U+FFFD cells (FR-008).
  - **Empty list**: `empty` text in `style->empty` on the first row; blank with no empty text.
  - **Custom styles**: a custom `selected` with an RGB background is combined as R310 says.
  - **SC-004**: with a count of 10,000,000, one draw in a 5-row area calls the callback exactly 5 times, for indices `top` to `top + 4` in order. After End, the calls are for n − 5 to n − 1.
  - **SC-001**: the quickstart's menu code, copied verbatim, at most 10 lines per frame, compiles and chooses entry 2 after Down, Enter.
- [ ] T311 [US1] Implement `src/list.h` (declaring `ovd_list_correct(OvdListState *, int32_t count)` for the table, R305) and `src/list.c`: the correction (data-model.md, R306, R307), `ovd_list_draw` (selected row filled across the width in the combined style, items through textfit), and `ovd_list_handle_event` (FR-011–FR-015). Append `src/list.c` to `CMakeLists.txt`. (Depends on T310.)
- [ ] T312 [US1] Proof: `debug`, `shared`, `sanitize`, and `windows-cross-debug` from clean, with 0 warnings and every test passing. (Depends on T311.)

## Phase 3 — OVD-27 Prompt (US2, P2; depends on OVD-25; parallel with OVD-26)

Goal: type one line of text (US2). Independent test:
`ctest --preset debug -R '^test_prompt$'`.

- [ ] T313 [US2] Write `tests/test_prompt.c`. After every step, check the screen (cursor cell in key `C`), `length`, `cursor`, `scroll`, the text bytes, and `buffer[length] == 0`:
  - **US2 scenarios 1–6.** Scenario 1 gives the text `hélXlo` with `cursor` 5.
  - **Graphemes** (R313):
    - `e` then U+0301 typed one code point at a time join into one grapheme, with the cursor after it;
    - a family emoji typed code point by code point through each ZWJ joins into one grapheme;
    - a base inserted before an existing combining mark takes the mark over, and the cursor ends after the merged grapheme;
    - a cluster longer than 15 bytes is moved over and deleted as one unit, and drawn as one U+FFFD.
  - **Outcomes** (FR-006):
    - Left at 0, Right at the end, Backspace at 0, and Delete at the end give `UNCHANGED`;
    - Home and End give `MOVED`;
    - Tab, Ctrl+A, Alt+B, F2, a typed U+0007, a typed U+D800 (R314), a mouse event, a resize, and paste start and end give `IGNORED`, with the state unchanged.
  - **Paste and filtering** (R315):
    - LF, CR, and TAB become spaces, and ESC, DEL, U+0085, U+2028, and U+202E are dropped;
    - a paste of only dropped controls gives `UNCHANGED`;
    - a paste in three chunks reads as one paste;
    - a grapheme split across two chunks joins.
  - **Capacity** (FR-016, FR-019):
    - capacity 0 writes nothing, and capacity 1 holds no text but keeps the NUL;
    - typing into a full buffer gives `TRUNCATED`, keeps the text unchanged, and writes nothing past `capacity`. Check with a guard byte after the buffer;
    - a paste that half fits keeps only whole graphemes;
    - a 2-code-point grapheme with room for one code point is left out whole.
  - **`ovd_prompt_set_text`**:
    - ill-formed input becomes U+FFFD where room allows, and `TRUNCATED` otherwise;
    - `nullptr` clears the text;
    - the cursor ends at the end, and `scroll` at 0.
  - **Direct-buffer repair** (R312):
    - with ill-formed bytes and controls written into the buffer and `length` past `capacity`, the next call clamps `length` and writes the NUL;
    - the next edit removes the bad bytes and shifts `cursor` by the bytes removed before it;
    - a draw changes no text byte.
  - **Scrolling** (FR-018):
    - in a 10-column area with 30 columns of text, the cursor cell is inside the area after every Left, Right, Home, End, and insertion;
    - a wide grapheme never shows half at the right edge;
    - the cursor at the end shows a space cell;
    - a 1-column area with a wide grapheme at the cursor shows a space in the cursor style;
    - rows below the first are untouched.
  - **FR-001 and FR-004**: the sentinel, clip, and `nullptr` cases as in T310, adapted to the prompt.
- [ ] T314 [US2] Implement `src/prompt.c`: entry correction and repair (R312), grapheme editing (R313), filtering (R315), `ovd_prompt_set_text`, `ovd_prompt_draw` (scroll per data-model.md, cursor combined per R310), and `ovd_prompt_handle_event` (FR-016–FR-020). It uses `ovd_unicode_next_grapheme`, `ovd_unicode_decode_utf8`, `ovd_unicode_encode_utf8`, and textfit. Append `src/prompt.c` to `CMakeLists.txt`. (Depends on T313.)
- [ ] T315 [US2] Proof: `debug`, `shared`, `sanitize`, and `windows-cross-debug` from clean, with 0 warnings and every test passing. (Depends on T314.)

## Phase 4 — OVD-28 Table (US3, P3; depends on OVD-26)

Goal: rows in aligned columns (US3). Independent test:
`ctest --preset debug -R '^test_table$'`.

- [ ] T316 [US3] Write `tests/test_table.c`, with keys `H` for the header style, `S` for selected, and `R` for a red-foreground cell:
  - **US3 scenarios 1–6.** Columns: `Status` FIXED 6, `Title` FILL 1, `Due` FIXED 10 right-aligned. In 40 columns that is 6 + 1 + 22 + 1 + 10.
  - **Widths** (R316), table-driven:
    - fill weights 1 and 2 over 10 cells give 4 and 6: floors 3 and 6, and the 1 cell left over goes to the leftmost fill column;
    - fixed columns wider than the area cut the last one shown;
    - no fill column leaves blank cells at the right;
    - `amount` 0, negative, and `INT32_MAX` (no overflow; data-model.md);
    - out-of-range `sizing` and `align` values (cast from 7) read as FIXED and LEFT;
    - `column_count` 0 or `nullptr` columns draws nothing.
  - **Cells**:
    - the callback's style is used, and on the selected row the red foreground survives the reverse highlight (R310);
    - text that fits is right-aligned, and cut text fills from the left;
    - the callback is called only for visible rows, row by row, left to right, with each call's `*style` pre-set to `style->normal`.
  - **Rows**:
    - a 1-row area shows the selected row with no header (R317);
    - an empty table shows `empty` below the header;
    - moving with `ovd_list_handle_event(&state, table.rows, &event)` scrolls the rows while the header stays (FR-023).
  - **SC-004**: with 10,000,000 rows, a draw asks for at most the visible rows × columns cells.
  - **FR-001 and FR-004**: the sentinel, clip, and `nullptr` cases as in T310.
- [ ] T317 [US3] Implement `src/table.c`: the width layout (R316), with 64-bit intermediate products and saturation; the header (R317); rows through `ovd_list_correct` and textfit; and per-cell style combination (FR-021–FR-023). Append `src/table.c` to `CMakeLists.txt`. (Depends on T316.)
- [ ] T318 [US3] Proof: `debug`, `shared`, `sanitize`, and `windows-cross-debug` from clean, with 0 warnings and every test passing. (Depends on T317.)

## Phase 5 — OVD-29 Fuzz and steady state (depends on OVD-26, OVD-27, OVD-28)

Goal: SC-003 and SC-005 across all three widgets. Independent test:
`ctest --preset sanitize -R '^test_(widget_fuzz|steady_state)$'`.

- [ ] T319 Write `tests/test_widget_fuzz.c` (R320), public API only:
  - **Generation**:
    - a seeded PRNG, with the seed printed and overridable through an argument;
    - at least 100,000 cases per widget, each a random state, area, content, and 1 to 8 events, with a draw after each;
    - values half from edge sets and half uniform;
    - list and table contents come from callbacks that record every index asked for;
    - the prompt gets a buffer with guard bytes on both sides.
  - **Checked after every call**:
    - no cell outside the area intersected with the clip differs from the sentinel, except a cell next to that edge whose wide-grapheme partner was inside, which must now be a space in the grapheme's style (the sentinel places wide graphemes across the edges in some cases);
    - the clip is restored;
    - callbacks were asked only for indices in `[0, count)`, and at most `page` rows a draw;
    - the list state satisfies data-model.md's correction;
    - the prompt invariant from data-model.md holds, and no guard byte changed.
  - **Output**: the case count, the seed, and the elapsed time.
  - **Fault injection**: with a deliberate fault (correction skipped, one clip restore removed, the NUL write removed), the test fails. Revert each fault, and record the three failures for the PR.
  - **Timing**: measure the `sanitize` run time. If it exceeds 60 s, set the test's own `TIMEOUT` in `tests/CMakeLists.txt` and record why. Never lower the count (R320, an ASSUMED item checked here).
- [ ] T320 Extend `tests/test_steady_state.c`. With the counting allocator, after the first frame, 10,000 cycles per widget of frame begin, draw, handle one key, and frame end, at a fixed size, make 0 allocator calls (SC-003, FR-005). The widgets cannot reach the allocator, so this guards the canvas calls they make.
- [ ] T321 Proof: `debug` and `sanitize` from clean, with the fuzz test's printed counts, seed, and time in the PR. Also `windows-cross-debug`. (Depends on T319, T320.)

## Phase 6 — OVD-30 Example, checklist, docs (depends on OVD-29 and 003's OVD-24)

- [ ] T322 [P] Write `examples/widget_demo.c` (R321), public API only:
  - a menu of four entries, a table of eight sample rows with one overdue date in red, and a prompt;
  - Tab moves between them; Escape on the menu quits, and destroy restores the terminal;
  - the menu's code is the quickstart's, so SC-001 holds as documented.

  Register it in `examples/CMakeLists.txt` with `ovd_target_warnings`.
- [ ] T323 [P] Add "Part 3: `widget_demo`" to `docs/validation/manual-checklist.md`, with the six steps from quickstart.md and a results template naming the terminal and its version (repo specifics).
- [ ] T324 [P] Add a "Widgets" section to `docs/architecture.md`: the modules and their paradigms (plan.md), caller-owned state and entry correction (R302), and the text-fitting rule (R308). Remove Widgets from "Not in this architecture (yet)", including the sentence "Layout, Motion, and Widgets have no present requirement yet", which is stale from the day this spec merges. In `README.md`, change "Layout, motion, and widgets are later features" to name only layout and motion, and add a short pointer to `widgets.h`. State only what the tests show (Art. XV).
- [ ] T325 Proof: the T309 preset list from clean, and `widget_demo` built in `debug`. Open a `needs-human` issue for Part 3 of the checklist in Windows Terminal and one POSIX terminal, and list the real-terminal steps in the PR as UNVERIFIED. (Depends on T322–T324.)

## Dependencies

- 003's OVD-23 → OVD-25 → OVD-26 → OVD-28; OVD-25 → OVD-27. OVD-26,
  OVD-27, and OVD-28 → OVD-29 → OVD-30. 003's OVD-24 → OVD-30.
- OVD-26 and OVD-27 run in parallel. Their files are disjoint, apart
  from one appended line each in `CMakeLists.txt` and
  `tests/CMakeLists.txt`.
- Inside a phase, test tasks come before the code they test, and the
  proof task comes last.

## Parallel examples

- **Phase 1**: T302, T303, T304, and T305 (after T302) touch different
  files. T306 and T307 can run in parallel once their tests exist.
- **After OVD-25**: one agent on OVD-26 (list), another on OVD-27
  (prompt).
- **Phase 6**: T322, T323, and T324 touch different files.

## Implementation strategy

- **MVP: OVD-25 and OVD-26.** The list and menu are what the first
  consumer needs most (US1). After OVD-26 merges, the consumer can start
  replacing its selection code.
- **OVD-27 (prompt)** runs in parallel with OVD-26, so text entry
  arrives with or right after the list.
- **OVD-28 (table)** reuses the list's correction and handler.
- **OVD-29** proves the three together under the sanitizers before
  **OVD-30** puts them on a real terminal (Art. V).

## Coverage

| Requirement | Tasks |
|---|---|
| FR-001 inside the area, clip restored | T310, T313, T316, T319 |
| FR-002 caller-owned state | T310 (two lists), T319; no static state in `src/` (review) |
| FR-003 correction | T310, T313, T316, T319 |
| FR-004 never fails | T310, T313, T316, T319 |
| FR-005 no allocation | T320 |
| FR-006 outcomes | T301 (enum size), T310, T313 |
| FR-007 caller text, no app types | T310 and T316 (callbacks with context) |
| FR-008 untrusted text | T304, T310, T313, T316 |
| FR-009 default and custom styles | T305, T310, T316 |
| FR-010 headless proof, readable screens | T302, T305; system terminal: T322, T325 |
| FR-011–FR-015 list | T310 |
| FR-016–FR-020 prompt | T303, T313 |
| FR-021–FR-023 table | T304, T316 |
| SC-001 menu in ≤ 10 lines | T310, T322 |
| SC-002 scenarios match | T310, T313, T316 |
| SC-003 0 allocations | T320 |
| SC-004 only visible rows | T310, T316 |
| SC-005 fuzz | T319 |
| SC-006 adoption | none: happens in the consumer's repository (spec SC-006) |

ASSUMED items in `research.md`: R320's run time, checked by T319.
