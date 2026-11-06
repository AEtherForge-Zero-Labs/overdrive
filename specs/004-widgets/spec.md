# Feature Specification: Widgets

**Feature Branch**: `004-widgets`

**Created**: 2026-11-06

**Status**: Draft

**Input**: "Feature 004: widgets for liboverdrive. Present requirement:
the first consumer (the owner's C23 console task manager, which vendors
Overdrive per feature 003) needs menus, prompts, lists, and tables
(named as a later feature, driven by that consumer, in
specs/003-packaging/spec.md "Out of scope"); today it must hand-build
them from ovd_canvas_text/ovd_canvas_box and its own key handling. Goal:
ergonomic, reusable widgets that reduce what downstream code has to
handle. Design: immediate mode with caller-owned state. Each widget is a
draw function into an OvdCanvas rect plus, where interactive, a handle
function that applies an OvdEvent to a small caller-owned state struct
(selection, scroll offset, cursor) and reports what changed. The library
re-validates and clamps state on every call (Art. I runtime validation).
No retained widget tree, no focus manager (the app routes events), no
Layout module (widgets take an OvdRect from the caller). Data comes
through caller-supplied text/accessors; no domain models (identity 4.6).
Candidate widgets: selectable scrolling list (a menu is a list variant),
table (columns, header, styled cells, wide-glyph-safe truncation and
clipping), single-line text prompt (grapheme-aware editing and cursor,
paste). Caller text stays untrusted (Art. XIII). Headless-first proof
with readable-text expected screens (Arts. V, VI). Out of scope:
layout/constraint solver, animation/Motion, mouse, multi-line text
editing, retained widget trees, focus management."

## Scope in one paragraph

An application gets three ready-made, keyboard-driven components: a
**list** that the user moves through and picks from (a menu is a list),
a **table** that shows rows in aligned columns with a header and the
same movement, and a **prompt** where the user types one line of text.
Each frame, the application asks the component to draw itself into a
rectangle the application chose. When a key arrives, the application
hands it to the component it considers active, and is told what
happened: the selection moved, an item was chosen, the text changed, the
user submitted, or the user cancelled. The small state that must
survive between frames, such as which row is selected or where the
cursor is, belongs to the application, which stores it however it
likes. The components never see the application's data types, only text
the application supplies. They handle the hard parts for it: scrolling
that keeps the selection visible, truncating and aligning text of mixed
widths without breaking a character, and editing text one user-visible
character at a time.

## User Scenarios & Testing *(mandatory)*

The users are developers of terminal applications in C that link
Overdrive. The first one is the owner's task manager, which shows tasks,
lets the user pick one or pick an action from a menu, and asks for typed
input such as a task title.

### User Story 1 — Pick from a list (Priority: P1)

A developer shows a list of items, for example tasks or menu entries,
in a rectangle of the screen. The user moves the highlight with the
arrow, page, home, and end keys and chooses an item with Enter, or backs
out with Escape. The developer supplies each item's text when the list
asks for it, and reacts to "chosen" or "cancelled".

**Why this priority**: Every screen of the first consumer is built
around picking something: a task, an action, a project. A menu is the
same component. Without it, the consumer keeps its own selection,
scrolling, and highlight logic, which is what this feature exists to
remove.

**Independent Test**: Against the headless terminal, draw a list of 50
items in a 20-by-5 rectangle, feed key presses, and compare each frame
with an expected screen written as rows of text with the highlighted row
named.

**Acceptance Scenarios**:

1. **Given** a list of 50 items in a rectangle 5 rows tall with the
   first item selected, **When** the user presses Down 7 times, **Then**
   item 8 is selected and visible, and the visible rows are items 4 to
   8.
2. **Given** that list, **When** the user presses Page Down, **Then** the
   selection moves by one visible page, and End then selects the last
   item and shows it on the bottom row.
3. **Given** a selected item, **When** the user presses Enter, **Then**
   the application is told that item was chosen, and the selection does
   not change. Escape tells it the list was cancelled.
4. **Given** a key the list does not use, such as F5 or a letter,
   **When** it is handed to the list, **Then** the list reports that it
   did not use the key and nothing changes, so the application can act
   on it.
5. **Given** the application removed items so the stored selection now
   points past the end, **When** the list is drawn or handed a key,
   **Then** the selection is moved to the last item, or to "nothing
   selected" when the list is empty, and drawing shows that.
6. **Given** a list of 0 items, **When** it is drawn, **Then** the
   rectangle shows the empty-list text the application supplied, or is
   left blank when it supplied none, and keys report "not used".

---

### User Story 2 — Type a line of text (Priority: P2)

A developer asks the user for one line of text, for example a task
title, in a rectangle one row tall. The user types, moves the cursor,
deletes, and pastes, and submits with Enter or cancels with Escape. The
developer gives the prompt a buffer of a size it chooses, and reads the
text when the user submits.

**Why this priority**: The first consumer creates and renames tasks, so
it needs text entry, and doing it correctly is the hardest part to
build by hand: characters made of several code points, wide characters,
paste, and a cursor that must never land inside a character. It ranks
below the list because the consumer can briefly keep its own prompt,
but it cannot work without picking.

**Independent Test**: Against the headless terminal, feed typed keys,
cursor keys, Backspace, Delete, and a paste into a prompt 10 columns
wide, and compare the screen, the cursor position, and the stored text
with expected values after each step.

**Acceptance Scenarios**:

1. **Given** an empty prompt, **When** the user types `héllo`, presses
   Left twice and types `X`, **Then** the text is `hélXlo` and the
   cursor sits after `X`.
2. **Given** text that contains a character made of several code points
   (for example `e` plus a combining accent, or a family emoji) or a
   wide character, **When** the user presses Left, Right, Backspace, or
   Delete across it, **Then** the cursor moves over, and deletion
   removes, the whole user-visible character, never part of it.
3. **Given** text longer than the prompt is wide, **When** the cursor
   moves, **Then** the visible part scrolls so that the cursor is always
   on screen, and a wide character is never shown cut in half.
4. **Given** a buffer that is almost full, **When** the user types or
   pastes more than fits, **Then** only whole characters that fit are
   inserted, the text stays valid, and the application is told the
   input was cut short.
5. **Given** a paste that contains newlines, tabs, or other controls,
   **When** it is inserted, **Then** the text stays one line: each
   newline or tab becomes a space, and other controls are dropped.
6. **Given** text, **When** the user presses Enter, **Then** the
   application is told the prompt was submitted and can read the text.
   Escape tells it the prompt was cancelled, and the text is left as it
   was.

---

### User Story 3 — Show rows in columns (Priority: P3)

A developer shows records, for example tasks with a status, a title,
and a due date, as a table: a header row and aligned columns, with the
same movement and choosing as the list. The developer supplies each
cell's text and, if it wants, a style for the cell, for example red for
an overdue date.

**Why this priority**: The first consumer already draws styled table
cells by hand (001 research §B). The table reuses the list's movement,
so it comes after the list.

**Independent Test**: Against the headless terminal, draw a table of 3
columns and 30 rows into a rectangle 40 columns wide, including cells
with wide characters and cells longer than their column. Compare each
frame with an expected screen.

**Acceptance Scenarios**:

1. **Given** columns with fixed widths and one column that takes the
   rest of the width, **When** the table is drawn, **Then** the header
   and every visible row line up, with the remaining width given to that
   column.
2. **Given** a cell whose text is wider than its column, **When** it is
   drawn, **Then** it is cut at a character boundary and ends with a
   truncation mark, and nothing spills into the next column.
3. **Given** a cell whose cut would fall in the middle of a wide
   character, **When** it is drawn, **Then** that character is left out
   and the column is padded, so the next column still starts in the
   right place.
4. **Given** a column set to align right, **When** a narrower value is
   drawn, **Then** it ends at the column's right edge.
5. **Given** the user moves the selection, **When** it goes past the
   last visible row, **Then** the rows scroll as in the list, and the
   header stays in place.
6. **Given** columns whose total width is more than the rectangle,
   **When** the table is drawn, **Then** columns are shown left to right
   until the width is used, and the last one shown is cut like a cell.

### Edge Cases

- **Zero, negative, or tiny rectangles.** A rectangle with no area draws
  nothing. A rectangle 1 column wide or 1 row tall draws what fits and
  never writes outside the rectangle (apart from FR-001's blank half
  of a wide character that straddles the edge).
- **A rectangle partly off the canvas.** Drawing is clipped as all
  canvas drawing is. The widget does not change the caller's clip.
- **Stored state that is out of range or contradictory.** A selection
  past the end, a negative scroll offset, a scroll offset that hides the
  selection, or a cursor in the middle of a character is corrected on
  the next draw or key, never read as is.
- **The rectangle shrinks between frames.** The selection stays
  selected and visible after the next draw.
- **Very large item counts.** A list of a million items costs the same
  per frame as a list of a hundred: only the visible rows are asked
  for.
- **Untrusted text.** Item, cell, header, and prompt text may hold
  invalid UTF-8, controls, or escape sequences. They are shown as the
  canvas shows them, with replacement characters, and never reach the
  terminal as control sequences.
- **Invalid initial prompt text.** Text the application gives the
  prompt to start from is made valid first: invalid bytes become
  replacement characters, as far as the buffer holds them, and controls
  follow the paste rule. If the application writes into the buffer
  directly instead, the prompt never reads past the stored length, and
  removes bytes that are not valid before the next edit. Removing
  cannot overflow the buffer; replacing could.
- **Keys with modifiers.** A key with Ctrl or Alt held is reported as
  not used unless this spec names it.
- **Pasted text split into several pieces.** The pieces are inserted in
  order. The text reads the same as one paste would.
- **A character made of several code points typed one code point at a
  time.** The code points join into one character as they arrive.
- **The application never calls the key handler.** Drawing alone still
  shows correct, corrected state. The key handler is optional.

## Requirements *(mandatory)*

### Functional Requirements

**All widgets**

- **FR-001**: Each widget MUST draw into a rectangle of the frame's
  canvas given by the application, and MUST NOT draw outside it or
  change the canvas clip as seen by the application after the call.
  The one exception is the canvas's existing rule that no wide
  character is left half-drawn. Where a wide character drawn earlier
  straddles the rectangle's edge and the widget draws over its inside
  half, its outside half becomes a blank.
- **FR-002**: The state that must persist between frames (the
  selection, the scroll positions, and the prompt's cursor and text)
  MUST be held by the application, in storage it provides. Overdrive
  MUST NOT keep any widget state of its own between calls.
- **FR-003**: Every draw and every key handling MUST first correct
  stored state into its valid range, as listed under Edge Cases, and
  MUST never read past the items, the text, or the buffer because of
  stored state.
- **FR-004**: Drawing and key handling MUST never fail, MUST accept any
  values without undefined behavior, and MUST treat missing inputs (no
  canvas, no state, no items) as doing nothing.
- **FR-005**: Drawing and key handling MUST NOT allocate memory.
- **FR-006**: Key handling MUST report one outcome per key: not used,
  used without change (for example Up on the first item, or Backspace
  at the start of the text), moved, chosen, cancelled, changed,
  submitted, or (prompt only) changed but cut short. A key reported as
  not used MUST leave state unchanged, apart from the correction in
  FR-003.
- **FR-007**: The application MUST supply all displayed text, as text or
  through a function the widget calls for each visible item. Widgets
  MUST NOT depend on any application data type.
- **FR-008**: All displayed text MUST be treated as untrusted, with the
  same replacement rules as canvas text. No byte of it may reach the
  terminal as a control sequence.
- **FR-009**: Widgets MUST provide a default appearance that marks the
  selected row and the prompt's cursor in a way that can be read on
  monochrome terminals, and MUST let the application set the style of
  normal rows, the selected row, the header, and the empty-list text.
- **FR-010**: Widgets MUST work identically with the headless and the
  system terminal. Every behavior in this spec MUST be shown on the
  headless terminal by tests whose expected screens are written as rows
  of text with styles named.

**List**

- **FR-011**: The list MUST support Up, Down, Page Up, Page Down, Home,
  and End to move the selection, Enter to choose, and Escape to cancel.
  Moving does not wrap around at either end.
- **FR-012**: After any draw or move, the selected item MUST be visible.
  The view MUST scroll by the smallest amount that keeps it visible.
- **FR-013**: Page Up and Page Down MUST move by the number of rows
  visible in the last drawn rectangle, and MUST stop at the first and
  last item.
- **FR-014**: The list MUST ask the application only for the items it
  draws in a frame.
- **FR-015**: An item wider than the rectangle MUST be cut at a
  character boundary with a truncation mark, as in FR-021.

**Prompt**

- **FR-016**: The prompt MUST edit text in a buffer whose capacity the
  application chooses. The text MUST always be valid UTF-8 and fit the
  buffer.
- **FR-017**: The prompt MUST support typing, Left, Right, Home, End,
  Backspace, Delete, Enter to submit, Escape to cancel, and paste. Each
  cursor movement and deletion MUST act on whole user-visible
  characters (grapheme clusters by the same rule the canvas uses).
- **FR-018**: The cursor MUST always sit at a character boundary, and
  MUST always be visible after a draw. The visible part of the text MUST
  scroll horizontally to keep it visible, without showing half of a
  wide character.
- **FR-019**: Insertion that does not fit MUST insert only the whole
  characters that fit, and report "changed but cut short".
- **FR-020**: Pasted and typed text MUST stay one line: newline,
  carriage return, and tab become a space, and other controls are
  dropped.

**Table**

- **FR-021**: The table MUST draw a header row and aligned columns.
  Each column has a title, a width that is either fixed in cells or a
  share of the remaining width, and a left or right alignment. Cells
  wider than their column MUST be cut at a character boundary and end
  with a truncation mark when at least 2 columns wide. A wide character
  that would be cut MUST be left out and its place padded.
- **FR-022**: The table MUST let the application give each cell its own
  style.
- **FR-023**: The table MUST move, scroll, choose, and cancel exactly as
  the list does (FR-011 to FR-014), with the header always shown when
  the rectangle is at least 2 rows tall.

### Key Entities

- **Widget state**: the small record the application keeps per widget
  between frames: for a list or table, the selected index and the first
  visible row; for a prompt, the text length, the cursor position, and
  the first visible column.
  Overdrive corrects it but does not own it.
- **Item source**: what the application provides so a list or table can
  get the text (and, for a table, the style) of item *i*, and the item
  count.
- **Column**: a table column's title, width rule, and alignment.
- **Outcome**: what one key did to a widget, from the closed set in
  FR-006 (eight values).
- **Appearance**: the styles for normal rows, the selected row, the
  header, the cursor, and the empty-list text, with defaults.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A developer shows a working, scrollable, choosable menu
  in at most 10 lines of application code per frame, not counting the
  item text.
- **SC-002**: For every acceptance scenario above, a headless test's
  expected screen matches, cell for cell and style for style.
- **SC-003**: Over 10,000 draw-and-key cycles on each widget, 0 memory
  allocations are made by the widgets.
- **SC-004**: Drawing a list or table asks for at most as many items as
  there are visible rows, whether the list holds 10 items or 10 million.
- **SC-005**: Across a seeded fuzz run of at least 100,000 random cases
  per widget (each a random state, rectangle, content, and sequence of
  up to 8 events, drawn after each event, with wide characters placed
  across the rectangle's edges), no widget writes outside its
  rectangle beyond the blank half that FR-001 allows, reads outside
  its inputs, or leaves the prompt's text
  invalid or its cursor inside a character.
- **SC-006**: The first consumer can replace its own selection,
  scrolling, and line-editing code with these widgets. Adoption itself
  happens in that project, not here.

## Assumptions

- **Immediate mode with caller-owned state** is the owner's decision
  (conversation of 2026-11-06). A retained widget tree is not needed by
  the first consumer.
- **No layout helpers.** The application computes rectangles itself.
  Splitting a rectangle is a few lines of arithmetic, and no consumer
  has yet shown a need for more (Art. VII). A later feature can add
  layout if one does.
- **No focus manager.** The application decides which widget gets each
  key. The first consumer shows one active widget at a time.
- **Keyboard only.** Mouse support is out of scope, as the first
  consumer works keyboard-only (002).
- **Moving does not wrap** at the ends of a list. Wrapping is a common
  alternative; not wrapping is the narrower behavior and the more
  common default in terminal applications.
- **The prompt uses only the keys in FR-017.** Word-wise movement and
  Ctrl-key editing shortcuts are left out until a consumer asks.
- **The truncation mark is a single-column ellipsis (…).** Terminals
  without that glyph show what their font has. A configurable mark is
  left out until a consumer asks.
- **A menu needs nothing beyond a list.** Shortcut letters for menu
  entries are left out until a consumer asks.
- **The prompt's text is UTF-8 in a buffer the application owns.**
  Overdrive never allocates or grows it.

## Out of scope

- Layout: splitting, constraints, or automatic sizing of rectangles.
- Motion: animation and transitions.
- Mouse input for widgets.
- Multi-line text editing, text areas, and word wrap.
- A retained widget tree, a focus manager, and event routing.
- Checkboxes, radio groups, progress bars, tabs, and dialogs.
- Changes to the existing canvas, input, or terminal behavior.

## Dependencies on decisions outside this spec

- None. Features 001 (rendering) and 002 (input) are merged into
  `main`. 003 (packaging) is specified, and its implementation is in
  progress. The first widget issue waits for 003's build-file issues,
  which own the files it edits (plan.md, Workstreams).
