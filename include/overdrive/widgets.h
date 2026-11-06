/*
 * Overdrive widgets: lists, tables, and a one-line prompt.
 *
 * PROPOSED CONTRACT (specs/004-widgets). Experimental for 0.x.
 *
 * Immediate mode. Each frame, the application draws a widget into a
 * rectangle of the frame's canvas, and hands it the events it chooses.
 * The state that lasts between frames is a small record the application
 * owns (OvdListState, OvdPromptState). Overdrive keeps no widget state of
 * its own, never allocates here, and never fails: every function accepts
 * any values, corrects the state record into its valid range on entry
 * (each record's comment lists the corrections), and treats a nullptr
 * canvas, state, or descriptor as doing nothing.
 *
 * Drawing writes only inside the rectangle intersected with the canvas's
 * current clip, and leaves the clip as it found it. The one exception is
 * the canvas's own rule (render.h, Drawing): where a wide grapheme
 * straddles that edge and the widget overwrites its inside half, the
 * outside half becomes a space in its own style. All displayed text is
 * untrusted and is drawn as ovd_canvas_text draws it (render.h): nothing
 * in it reaches the terminal as a control sequence.
 */
#ifndef OVERDRIVE_WIDGETS_H
#define OVERDRIVE_WIDGETS_H

#include <overdrive/core.h>
#include <overdrive/render.h>
#include <overdrive/input.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- Text and outcomes ----------------------------------------------- */

/* A view of UTF-8 text the application owns: length bytes at bytes, not
 * necessarily NUL-terminated. Transparent: a plain (pointer, length)
 * value returned per item, like OvdPasteText. bytes == nullptr reads as
 * empty text whatever length says. */
typedef struct OvdText {
    const char *bytes;
    size_t length;
} OvdText;

/* A view of a NUL-terminated string; nullptr gives empty text. */
static inline OvdText ovd_text(const char *string)
{
    return (OvdText){ .bytes = string, .length = string ? strlen(string) : 0 };
}

/* What one event did to a widget (spec FR-006). */
typedef enum OvdOutcome : uint8_t {
    /* The widget does not use this event; state is unchanged apart from
     * entry correction. The application may act on it. */
    OVD_OUTCOME_IGNORED = 0,
    /* The widget used the event, and nothing changed: Up on the first
     * item, Backspace at the start of the text. */
    OVD_OUTCOME_UNCHANGED,
    /* The selection or the cursor moved. */
    OVD_OUTCOME_MOVED,
    /* Enter on a list or table with an item selected. */
    OVD_OUTCOME_CHOSEN,
    /* Escape. State is unchanged. */
    OVD_OUTCOME_CANCELLED,
    /* The prompt's text changed. */
    OVD_OUTCOME_CHANGED,
    /* The prompt's text changed, but not all of the input fitted. */
    OVD_OUTCOME_TRUNCATED,
    /* Enter on a prompt. */
    OVD_OUTCOME_SUBMITTED,
} OvdOutcome;

/* ---- Appearance ------------------------------------------------------ */

/* Styles a widget draws with. Transparent: plain values with no
 * invariant beyond their fields, set field by field by the application.
 *
 * In the selected row, each cell's own style is combined with selected:
 * selected's attrs are added, and each of selected's colors that is not
 * OVD_COLOR_KIND_DEFAULT replaces the cell's. So a table cell's own
 * foreground survives the default reverse-video highlight. */
typedef struct OvdWidgetStyle {
    OvdStyle normal;   /* rows and prompt text */
    OvdStyle selected; /* combined into the selected row, as above */
    OvdStyle header;   /* table header row */
    OvdStyle empty;    /* the empty-list text */
    OvdStyle cursor;   /* the prompt's cursor cell, combined like selected */
} OvdWidgetStyle;

/* Readable on monochrome terminals: normal is OVD_STYLE_DEFAULT; selected
 * and cursor add OVD_ATTR_REVERSE; header adds OVD_ATTR_BOLD; empty adds
 * OVD_ATTR_DIM. Every draw function treats a nullptr style as this. */
OVD_API OvdWidgetStyle ovd_widget_style_default(void);

/* ---- List and table state -------------------------------------------- */

/* The state of a list or a table between frames. Transparent on purpose
 * (plan, Public API): the application sets selected directly, for
 * example to select a row it just inserted, and only the application
 * knows the item count, which changes outside Overdrive.
 *
 * Corrected on entry to every function, given the item count n:
 * - n <= 0: selected = -1 and top = 0;
 * - otherwise: selected is clamped to [0, n - 1], then top is moved by
 *   the least amount that shows selected, within [0, n - 1];
 * - page < 1 reads as 1.
 * Drawing also stores the number of item rows it showed in page, which
 * Page Up and Page Down use (FR-013). Zero-initialize to start: the first
 * item is selected once there is one. */
typedef struct OvdListState {
    int32_t selected; /* index of the selected item, or -1 for none */
    int32_t top;      /* index of the first visible item */
    int32_t page;     /* item rows shown by the last draw */
} OvdListState;

/* Returns the text of item index, 0 <= index < count. Called only for the
 * items a draw shows (FR-014), in order, during that draw. The returned
 * bytes must stay valid until the callback is next called or the draw
 * returns. */
typedef OvdText (*OvdItemText)(void *context, int32_t index);

/* For a context that is a const char *const array of count strings:
 * returns ovd_text(array[index]). Lets a menu of literals be a list
 * without a callback of its own (SC-001). */
OVD_API OvdText ovd_text_array_item(void *context, int32_t index);

/* A list's content for one call. A count <= 0, or a nullptr text, is an
 * empty list. */
typedef struct OvdList {
    int32_t count;
    OvdItemText text;
    void *context;
    OvdText empty; /* drawn on the first row when the list is empty */
} OvdList;

/* Draws one item per row of area, from state->top, with the selected row
 * filled across the whole width in the combined style. An item wider than
 * area is cut at a grapheme boundary and ends with U+2026 when area is at
 * least 2 columns wide; a wide grapheme that would be cut is left out and
 * its place padded. An empty list draws list->empty in style->empty on
 * the first row. */
OVD_API void ovd_list_draw(OvdCanvas *canvas, OvdRect area, OvdListState *state,
                           const OvdList *list, const OvdWidgetStyle *style);

/* Applies event to a list or a table of count items, after the entry
 * correction above.
 *
 * Keys without OVD_MODIFIER_CTRL or OVD_MODIFIER_ALT: Up and Down move by
 * one; Page Up and Page Down move by state->page; Home and End go to the
 * first and last item; moving stops at both ends, and a move that cannot
 * go further returns OVD_OUTCOME_UNCHANGED. Enter returns
 * OVD_OUTCOME_CHOSEN with state->selected the chosen item. Escape returns
 * OVD_OUTCOME_CANCELLED. With count <= 0, every event returns
 * OVD_OUTCOME_IGNORED. Every other event, including every key with Ctrl
 * or Alt held, returns OVD_OUTCOME_IGNORED. A nullptr state or event
 * returns OVD_OUTCOME_IGNORED. */
OVD_API OvdOutcome ovd_list_handle_event(OvdListState *state, int32_t count,
                                         const OvdEvent *event);

/* ---- Table ----------------------------------------------------------- */

typedef enum OvdAlign : uint8_t {
    OVD_ALIGN_LEFT = 0,
    OVD_ALIGN_RIGHT,
} OvdAlign;

typedef enum OvdColumnSizing : uint8_t {
    /* amount is the column's width in cells. */
    OVD_COLUMN_FIXED = 0,
    /* amount is the column's share of the width the fixed columns and
     * gaps leave. */
    OVD_COLUMN_FILL,
} OvdColumnSizing;

/* One column. Transparent: a plain description the application fills in.
 * Out-of-range values read as: sizing and align as their 0 value; a
 * negative or zero amount as 0 cells, or as a share of 0 (no width).
 * When every fill column's share is 0, the fill columns get no width. */
typedef struct OvdColumn {
    OvdText title;
    OvdColumnSizing sizing;
    int32_t amount;
    OvdAlign align;
} OvdColumn;

/* Stores the text of the cell at (row, column) in *text. *style holds
 * style->normal on entry, and the callback may change it (FR-022).
 * Called only for the rows a draw shows, row by row, left to right. The
 * text must stay valid until the callback is next called or the draw
 * returns. */
typedef void (*OvdCellText)(void *context, int32_t row, int32_t column,
                            OvdText *text, OvdStyle *style);

/* A table's content for one call. rows <= 0, or a nullptr cell, is an
 * empty table; column_count <= 0, or nullptr columns, draws nothing. */
typedef struct OvdTable {
    const OvdColumn *columns;
    int32_t column_count;
    int32_t rows;
    OvdCellText cell;
    void *context;
    OvdText empty;
} OvdTable;

/* Draws a header row of column titles in style->header, then one table
 * row per remaining row of area, laid out as in the list. With area one
 * row tall, there is no header and the one row shows state->selected.
 *
 * Widths: one blank column separates neighbouring columns. Fixed columns
 * take their amount. The width left after them and the gaps, if any, is
 * split among fill columns in proportion to amount, rounding down, and
 * the cells left over go one each to the fill columns from the left.
 * Columns are laid out from the left until area's width runs out; the
 * last one shown is cut at the edge. A cell whose text fits is aligned
 * within its column; a cell that is cut fills its column from the left,
 * marked and padded as a list item is.
 *
 * Use ovd_list_handle_event with table->rows as the count for movement:
 * the table shares OvdListState. */
OVD_API void ovd_table_draw(OvdCanvas *canvas, OvdRect area, OvdListState *state,
                            const OvdTable *table, const OvdWidgetStyle *style);

/* ---- Prompt ---------------------------------------------------------- */

/* The state of a one-line prompt, editing text in a buffer the
 * application owns. Transparent on purpose (plan, Public API): the
 * application reads the text from buffer and length, and supplies its own
 * storage of any size. Overdrive never allocates or grows it.
 *
 * buffer holds capacity bytes. The text is buffer[0, length). Overdrive
 * keeps buffer[length] == '\0', so the text is also a C string, and so at
 * most capacity - 1 bytes are text. cursor and scroll are byte offsets
 * into the text.
 *
 * Corrected on entry to every function:
 * - buffer == nullptr or capacity == 0: no text, nothing is written;
 * - length is clamped to capacity - 1, and buffer[length] = '\0';
 * - before an edit, bytes that are not well-formed UTF-8, and controls,
 *   are removed in place (removing never needs more room);
 * - cursor and scroll are clamped to length, then moved back to the
 *   grapheme boundary at or before them (render.h's grapheme rule);
 * - drawing moves scroll by whole graphemes so that the cursor's cell is
 *   inside the area.
 * To start from given text, use ovd_prompt_set_text rather than writing
 * buffer directly: it repairs invalid input with U+FFFD where the buffer
 * has room. */
typedef struct OvdPromptState {
    char *buffer;
    size_t capacity;
    size_t length;
    size_t cursor; /* byte offset of the cursor, on a grapheme boundary */
    size_t scroll; /* byte offset of the first visible grapheme */
} OvdPromptState;

/* Replaces the text with length bytes of text, filtered as pasted text is
 * (ovd_prompt_handle_event), with each ill-formed UTF-8 subpart replaced
 * by U+FFFD. Places the cursor at the end, and scroll at 0. Returns
 * OVD_OUTCOME_CHANGED, or OVD_OUTCOME_TRUNCATED when not all of it fitted:
 * whole graphemes are kept from the start while they fit. text == nullptr
 * clears the text. A nullptr state returns OVD_OUTCOME_IGNORED. */
OVD_API OvdOutcome ovd_prompt_set_text(OvdPromptState *state, const char *text, size_t length);

/* Draws the visible part of the text on the first row of area, from
 * scroll, in style->normal, and the cursor's cell (the grapheme at the
 * cursor, or a space at the end of the text) combined with style->cursor.
 * A wide grapheme that does not fit at the right edge is left out, and
 * its place padded. Rows below the first are left as they were. */
OVD_API void ovd_prompt_draw(OvdCanvas *canvas, OvdRect area, OvdPromptState *state,
                             const OvdWidgetStyle *style);

/* Applies event to the prompt, after the entry correction above.
 *
 * Keys without OVD_MODIFIER_CTRL or OVD_MODIFIER_ALT:
 * - OVD_KEY_CHAR inserts the code point at the cursor and returns
 *   OVD_OUTCOME_CHANGED (or OVD_OUTCOME_TRUNCATED, below); a control
 *   code point, a surrogate, or a value above U+10FFFF returns
 *   OVD_OUTCOME_IGNORED. After any insertion the cursor is
 *   at the end of the grapheme that holds the last inserted byte, so it
 *   stays on a boundary even when the text after it attaches to the
 *   insertion;
 * - Left, Right, Home, End move the cursor by whole graphemes and return
 *   OVD_OUTCOME_MOVED, or OVD_OUTCOME_UNCHANGED when the cursor is
 *   already at that end;
 * - Backspace and Delete remove the whole grapheme before or at the
 *   cursor and return OVD_OUTCOME_CHANGED, or OVD_OUTCOME_UNCHANGED when
 *   there is nothing before or at the cursor;
 * - Enter returns OVD_OUTCOME_SUBMITTED; Escape returns
 *   OVD_OUTCOME_CANCELLED; neither changes the text;
 * - Tab and every other key return OVD_OUTCOME_IGNORED, so the
 *   application can use them (for example, Tab to change widgets).
 * OVD_EVENT_KIND_PASTE_TEXT inserts its text at the cursor: newline,
 * carriage return, and tab become a space, other controls are dropped,
 * and it returns OVD_OUTCOME_CHANGED, or OVD_OUTCOME_UNCHANGED when
 * nothing is left after filtering. Paste start and end markers, and every other event, return
 * OVD_OUTCOME_IGNORED.
 *
 * Insertion keeps whole graphemes while they fit in capacity - 1 bytes
 * and returns OVD_OUTCOME_TRUNCATED if any input was left out. A code
 * point that joins the grapheme before the cursor (a combining mark, or
 * the code point after a ZWJ) joins it. A nullptr state or event returns
 * OVD_OUTCOME_IGNORED. */
OVD_API OvdOutcome ovd_prompt_handle_event(OvdPromptState *state, const OvdEvent *event);

#endif /* OVERDRIVE_WIDGETS_H */
