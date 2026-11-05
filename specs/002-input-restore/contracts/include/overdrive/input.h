/*
 * Overdrive input: events read from the terminal (keys, mouse, paste,
 * focus, resize).
 *
 * PROPOSED CONTRACT (specs/002-input-restore). Experimental for 0.x.
 *
 * Input is enabled per terminal at create (OvdTerminalConfig.input,
 * terminal.h). Every byte read from the terminal is untrusted: decoding is
 * bounded, unknown sequences are discarded, and no input byte is written
 * to the terminal. Neither function here allocates.
 *
 * Threading: as terminal.h. A wait cannot be woken from another thread.
 */
#ifndef OVERDRIVE_INPUT_H
#define OVERDRIVE_INPUT_H

#include <overdrive/core.h>
#include <overdrive/terminal.h>

/* Wait until an event arrives, however long that takes. Any negative
 * timeout means the same. */
constexpr int32_t OVD_WAIT_FOREVER = -1;

typedef enum OvdEventKind : uint8_t {
    /* The wait's timeout elapsed with no event. */
    OVD_EVENT_KIND_NONE = 0,
    OVD_EVENT_KIND_KEY,
    OVD_EVENT_KIND_MOUSE,
    /* A paste: START, then zero or more TEXT chunks, then END. Keys never
     * arrive between START and END. */
    OVD_EVENT_KIND_PASTE_START,
    OVD_EVENT_KIND_PASTE_TEXT,
    OVD_EVENT_KIND_PASTE_END,
    OVD_EVENT_KIND_FOCUS_IN,
    OVD_EVENT_KIND_FOCUS_OUT,
    /* The terminal's size changed. The next ovd_frame_begin still re-reads
     * it, and may see a newer size. */
    OVD_EVENT_KIND_RESIZE,
} OvdEventKind;

typedef enum OvdKey : uint16_t {
    /* A character: OvdKeyEvent.codepoint holds one Unicode scalar value.
     * Ctrl+letter is the lower-case letter with OVD_MODIFIER_CTRL. */
    OVD_KEY_CHAR = 0,
    OVD_KEY_ENTER,
    OVD_KEY_TAB,
    OVD_KEY_BACKSPACE,
    OVD_KEY_ESCAPE,
    OVD_KEY_UP,
    OVD_KEY_DOWN,
    OVD_KEY_LEFT,
    OVD_KEY_RIGHT,
    OVD_KEY_HOME,
    OVD_KEY_END,
    OVD_KEY_PAGE_UP,
    OVD_KEY_PAGE_DOWN,
    OVD_KEY_INSERT,
    OVD_KEY_DELETE,
    OVD_KEY_F1,
    OVD_KEY_F2,
    OVD_KEY_F3,
    OVD_KEY_F4,
    OVD_KEY_F5,
    OVD_KEY_F6,
    OVD_KEY_F7,
    OVD_KEY_F8,
    OVD_KEY_F9,
    OVD_KEY_F10,
    OVD_KEY_F11,
    OVD_KEY_F12,
} OvdKey;

/* Modifier values. A set of them is a uint8_t of OR-ed values (the same
 * resolution as OvdAttr in render.h). Terminals do not report every
 * modifier for every key: legacy encodings cannot tell Ctrl+I from Tab,
 * Ctrl+M from Enter, Ctrl+[ from Escape, or (on some terminals) Ctrl+H
 * from Backspace, and report the named key. */
typedef enum OvdModifier : uint8_t {
    OVD_MODIFIER_SHIFT = 1u << 0,
    OVD_MODIFIER_ALT = 1u << 1,
    OVD_MODIFIER_CTRL = 1u << 2,
} OvdModifier;

typedef struct OvdKeyEvent {
    OvdKey key;
    /* For OVD_KEY_CHAR, a Unicode scalar value (never a surrogate, never
     * above U+10FFFF; ill-formed input arrives as U+FFFD). 0 otherwise. */
    uint32_t codepoint;
    /* A set of OvdModifier values. */
    uint8_t modifiers;
} OvdKeyEvent;

typedef enum OvdMouseAction : uint8_t {
    OVD_MOUSE_ACTION_PRESS = 0,
    OVD_MOUSE_ACTION_RELEASE,
    /* Motion with a button held: one event per new cell. */
    OVD_MOUSE_ACTION_DRAG,
    /* One wheel step; button is OVD_MOUSE_BUTTON_WHEEL_UP or _DOWN. */
    OVD_MOUSE_ACTION_WHEEL,
} OvdMouseAction;

typedef enum OvdMouseButton : uint8_t {
    /* A release or drag whose button the terminal did not report. */
    OVD_MOUSE_BUTTON_NONE = 0,
    OVD_MOUSE_BUTTON_LEFT,
    OVD_MOUSE_BUTTON_MIDDLE,
    OVD_MOUSE_BUTTON_RIGHT,
    OVD_MOUSE_BUTTON_WHEEL_UP,
    OVD_MOUSE_BUTTON_WHEEL_DOWN,
} OvdMouseButton;

typedef struct OvdMouseEvent {
    OvdMouseAction action;
    OvdMouseButton button;
    /* 0-based cell, column x and row y, as the terminal reported it. It
     * can lie outside the current size when a resize raced the report. */
    int32_t x;
    int32_t y;
    /* A set of OvdModifier values. */
    uint8_t modifiers;
} OvdMouseEvent;

/* A chunk of pasted text: valid UTF-8, never ending inside a scalar
 * value, possibly containing controls (it is data: drawing it is subject
 * to render.h's control replacement). bytes is owned by the terminal and
 * valid until the next ovd_input_wait, ovd_input_feed, or
 * ovd_terminal_destroy on it. length > 0. */
typedef struct OvdPasteText {
    const char *bytes;
    size_t length;
} OvdPasteText;

/* One event. Read the member that kind names; the others are zero.
 * Transparent: a plain value read once per event (plan, Public API). */
typedef struct OvdEvent {
    OvdEventKind kind;
    union {
        OvdKeyEvent key;       /* OVD_EVENT_KIND_KEY */
        OvdMouseEvent mouse;   /* OVD_EVENT_KIND_MOUSE */
        OvdPasteText paste;    /* OVD_EVENT_KIND_PASTE_TEXT */
        OvdSize size;          /* OVD_EVENT_KIND_RESIZE */
    };
} OvdEvent;

/* Waits for the next event, for at most timeout_ms milliseconds: 0 polls,
 * a negative value (OVD_WAIT_FOREVER) waits indefinitely. Returns as soon
 * as an event is available; only a lone ESC waits, for at most the
 * terminal's escape delay, to tell the Escape key from the start of a
 * sequence. On OVD_OK, *event is the event, or OVD_EVENT_KIND_NONE if the
 * timeout elapsed.
 *
 * Headless backend: never sleeps. The timeout counts as the time that
 * passed, so a pending lone ESC becomes the Escape key when timeout_ms is
 * at least the escape delay or negative. With nothing queued, a negative
 * timeout returns OVD_EVENT_KIND_NONE at once: nothing else can arrive. A size
 * stored by ovd_terminal_set_size is reported once as OVD_EVENT_KIND_RESIZE.
 *
 * Fails with OVD_ERROR_INVALID_ARGUMENT for nullptr; OVD_ERROR_STATE if
 * the terminal was created with input off, or from inside its sink;
 * OVD_ERROR_PLATFORM if reading the terminal failed. Never allocates. */
[[nodiscard]] OVD_API OvdResult ovd_input_wait(OvdTerminal *terminal, int32_t timeout_ms,
                                               OvdEvent *event);

/* Headless backend only (OVD_ERROR_UNSUPPORTED otherwise): queues bytes
 * as if the terminal had sent them. Takes as many as the input queue has
 * room for, possibly 0, and stores that count in *accepted; the rest are
 * the caller's to feed after waiting. Fails with
 * OVD_ERROR_INVALID_ARGUMENT for a nullptr terminal or accepted, or
 * nullptr bytes with length > 0; OVD_ERROR_STATE if input is off. Never
 * allocates. */
[[nodiscard]] OVD_API OvdResult ovd_input_feed(OvdTerminal *terminal, const char *bytes,
                                               size_t length, size_t *accepted);

#endif /* OVERDRIVE_INPUT_H */
