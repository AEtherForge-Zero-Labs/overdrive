# Data Model: Widgets

All types are in the proposed
[`contracts/include/overdrive/widgets.h`](contracts/include/overdrive/widgets.h).
This page lists their fields, the invariants that are checked at run
time, and how state changes. Decisions are cited as R3xx
([research.md](research.md)).

## OvdListState (caller-owned, shared by the list and the table)

| Field | Type | Meaning |
|---|---|---|
| `selected` | `int32_t` | selected item, or −1 for none |
| `top` | `int32_t` | first visible item |
| `page` | `int32_t` | item rows shown by the last draw (R306) |

**Correction on entry** (every draw and handle, given the count n):

1. n ≤ 0 → `selected = −1`, `top = 0`.
2. Else `selected = clamp(selected, 0, n − 1)`. A negative value,
   including −1, becomes 0.
3. `page = max(page, 1)`.
4. If `selected < top`, then `top = selected`. If
   `selected ≥ top + page`, then `top = selected − page + 1`. Then
   `top = clamp(top, 0, n − 1)` (R307).

The draw sets `page` to the number of item rows it shows, then applies
step 4 again with the new page, so a shrunken area still shows the
selection (spec Edge Cases).

**Transitions** (`ovd_list_handle_event`, no Ctrl or Alt held):

| Key | New `selected` | Outcome |
|---|---|---|
| Up / Down | ± 1, clamped | `MOVED`, or `UNCHANGED` at an end |
| Page Up / Page Down | ± `page`, clamped | `MOVED` or `UNCHANGED` |
| Home / End | 0 / n − 1 | `MOVED` or `UNCHANGED` |
| Enter | same | `CHOSEN` |
| Escape | same | `CANCELLED` |
| anything else, or n ≤ 0 | same | `IGNORED` |

## OvdList and OvdTable (per call, caller-owned, read-only)

| Type | Fields |
|---|---|
| `OvdList` | `count`, `text` (`OvdItemText`), `context`, `empty` |
| `OvdTable` | `columns`, `column_count`, `rows`, `cell` (`OvdCellText`), `context`, `empty` |
| `OvdColumn` | `title`, `sizing` (`OVD_COLUMN_FIXED` / `_FILL`), `amount`, `align` (`OVD_ALIGN_LEFT` / `_RIGHT`) |

**Validation**: a count ≤ 0 or a nullptr callback means empty. For a
table, `column_count ≤ 0` or nullptr `columns` draws nothing. An
out-of-range `sizing` or `align` reads as its 0 value, and an
`amount ≤ 0` reads as 0. A callback is called only with
`0 ≤ index < count` and `top ≤ index < top + page`. A returned
`OvdText` with nullptr `bytes` is empty.

**Column widths** (R316), for area width W, with k columns:

```
gaps   = k − 1
fixed  = Σ amount over FIXED columns
rest   = max(0, W − fixed − gaps)
fill_i = floor(rest × amount_i / Σ amount over FILL columns)
         + 1 for the first (rest − Σ floor) FILL columns, from the left
fill_i = 0 for every FILL column when Σ amount over FILL columns is 0
```

The computation saturates rather than overflowing (each term is
clamped to `INT32_MAX`; the product is computed in 64 bits).

## OvdPromptState (caller-owned)

| Field | Type | Meaning |
|---|---|---|
| `buffer` | `char *` | caller's storage |
| `capacity` | `size_t` | bytes at `buffer` |
| `length` | `size_t` | bytes of text, ≤ capacity − 1 |
| `cursor` | `size_t` | byte offset, on a grapheme boundary |
| `scroll` | `size_t` | byte offset of the first visible grapheme |

**Invariant after any call** (checked by `test_widget_fuzz`, R320):

- `buffer == nullptr || capacity == 0`: no text, nothing written.
- Otherwise: `length ≤ capacity − 1`, `buffer[length] == '\0'`, and
  after an edit, `buffer[0, length)` is well-formed UTF-8 with no code
  point from the R315 dropped set.
- `scroll ≤ cursor ≤ length`, both on grapheme boundaries as
  `ovd_unicode_next_grapheme` segments the text from offset 0.

**Correction on entry**: clamp `length`, write the NUL, clamp `cursor`
and `scroll` to `length`, and snap each back to the boundary at or
before it. Before an edit (`handle_event` with an editing key or a
paste), remove ill-formed subparts and dropped code points in place,
adjusting `cursor` by the bytes removed before it (R312).

**Transitions** (`ovd_prompt_handle_event`, no Ctrl or Alt held):

| Event | Effect | Outcome |
|---|---|---|
| `OVD_KEY_CHAR`, printable | insert at cursor (R313) | `CHANGED`, `TRUNCATED` if it did not fit |
| `OVD_KEY_CHAR`, control or not a scalar value | none | `IGNORED` |
| Left / Right / Home / End | cursor by grapheme | `MOVED`, or `UNCHANGED` at an end |
| Backspace / Delete | remove one grapheme | `CHANGED`, or `UNCHANGED` at an end |
| Enter | none | `SUBMITTED` |
| Escape | none | `CANCELLED` |
| `PASTE_TEXT` | filtered insert (R315) | `CHANGED`, `TRUNCATED`, or `UNCHANGED` if nothing remained after filtering |
| Tab, other keys, other events | none | `IGNORED` |

**Draw**: on the first row of the area, show graphemes from `scroll`.
First, if `cursor < scroll`, set `scroll = cursor`. Then, while the
cursor cell would end past the right edge, advance `scroll` by one
grapheme. The cursor cell is the grapheme at `cursor`, or one space at
the end of the text. If the area is narrower than the cursor cell (a
wide grapheme in a 1-column area), the cell is drawn as a space in the
cursor style.

## OvdWidgetStyle and OvdOutcome

`OvdWidgetStyle` holds five `OvdStyle` values; defaults in R311,
combination in R310. `OvdOutcome` is a closed set of eight values
(R304).

## Internal: text fitting (src/textfit.h)

Transformational (R308). Inputs: text, a width w ≥ 0, an alignment.
Output: the byte span to draw, its width, whether to add U+2026, and
the left and right padding. Rules:

- The text fits when its total width is ≤ w; it is then drawn whole and
  aligned: left puts the padding on the right, right puts it on the left.
- Otherwise, with w ≥ 2: keep graphemes while their total width is
  ≤ w − 1, then add U+2026 right after them, then pad on the right to w.
  If a wide grapheme would cross that limit, it is left out, so the pad
  is 1. Cut text always fills from the left, whatever the alignment:
  for example `ab中` in w = 3 is `ab…`, and `a中b` in w = 3 is `a… `.
- With w == 1 and text that does not fit: draw the first grapheme if it
  is 1 wide, and otherwise a space. No mark.
- With w == 0: nothing.
