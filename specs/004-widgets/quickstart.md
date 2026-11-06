# Quickstart: Widgets

How to check that feature 004 works, once it is implemented. The
behavior is in [spec.md](spec.md), and the API is in
[contracts/include/overdrive/widgets.h](contracts/include/overdrive/widgets.h).

## Prerequisites

The machine baseline (`.specify/memory/baseline.md`), and a clean
checkout of `main` with this feature merged.

## Headless proof (every platform, CI)

```bash
cmake --preset debug && cmake --build --preset debug
ctest --preset debug -R 'test_(unicode|textfit|widget|list|table|prompt|widget_fuzz|steady_state)$'

cmake --preset sanitize && cmake --build --preset sanitize
ctest --preset sanitize -R 'test_widget_fuzz'

cmake --preset windows-cross-debug && cmake --build --preset windows-cross-debug
ctest --preset windows-cross-debug -R 'test_(list|table|prompt)'
```

What passing shows:

| Test | Shows |
|---|---|
| `test_unicode` | the UTF-8 encoder the prompt uses for typed keys (R314), alongside the existing cases |
| `test_textfit` | cutting, the ellipsis, wide-grapheme padding, alignment (FR-015, FR-021) |
| `test_widget` | the default appearance (FR-009) and the string-array helper (SC-001) |
| `test_list` | US1 scenarios 1–6 as expected screens with styles named (SC-002) |
| `test_prompt` | US2 scenarios 1–6, plus cursor and text after each step |
| `test_table` | US3 scenarios 1–6 |
| `test_widget_fuzz` | at least 10⁵ cases per widget, with no write outside the area beyond FR-001's blank half of a wide grapheme across an edge, no out-of-range callback, and the prompt invariant held (SC-005); prints its case count and seed |
| `test_steady_state` | 0 allocator calls over 10,000 cycles per widget (SC-003) |

SC-004 (only the visible rows are asked for) is a case in `test_list`
and `test_table`: a source of 10,000,000 items counts its callback
calls.

## A menu in application code (SC-001)

The shape a consumer writes per frame, with `entries` a
`const char *const[]`:

```c
OvdList menu = { .count = 3, .text = ovd_text_array_item, .context = (void *)entries };
ovd_list_draw(canvas, (OvdRect){ 2, 2, 20, 5 }, &menu_state, &menu, nullptr);
/* ... after ovd_frame_end, per event: */
switch (ovd_list_handle_event(&menu_state, menu.count, &event)) {
case OVD_OUTCOME_CHOSEN:    open(menu_state.selected); break;
case OVD_OUTCOME_CANCELLED: close_menu(); break;
default: break;
}
```

## Real terminal (manual, UNVERIFIED until a human runs it)

```bash
cmake --preset debug && cmake --build --preset debug
./build/debug/bin/widget_demo
```

The steps are in `docs/validation/manual-checklist.md` (widget
section). Record the terminal and its version with each observation:

1. Move through the menu with arrows, Page Up/Down, Home, and End. The
   highlight stays visible.
2. Press Tab to reach the table. An overdue date stays red on the
   highlighted row.
3. Tab to the prompt, and type `héllo`, then a CJK word and an emoji.
   Move across them with Left and Right; the cursor never splits one.
4. Paste text containing a newline. It arrives as one line.
5. Resize the window smaller. Every widget stays inside its area, and
   the selection stays visible.
6. Quit. The terminal is restored.
