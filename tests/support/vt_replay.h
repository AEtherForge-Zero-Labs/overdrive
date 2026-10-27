/*
 * Test-only replay interpreter: applies Overdrive's terminal output to a
 * screen model, accepting exactly the protocol the library may emit
 * (research R7, R33, R34, R38) and rejecting everything else, so
 * unexpected output fails the test that produced it.
 *
 * Accepted, and nothing more:
 *   CSI r;c H       CUP; both parameters explicit, 1-based, in range
 *   CSI p;...;p m   SGR: 0 1 2 3 4 7 9 30-37 38;5;n 38;2;r;g;b 39
 *                        40-47 48;5;n 48;2;r;g;b 49 90-97 100-107
 *   CSI 2 J         ED 2, only in the default SGR state
 *   CSI ? 25 h/l    DECTCEM     CSI ? 7 h/l     DECAWM
 *   CSI ? 1049 h/l  alternate screen
 *   CSI ? 2026 h/l  synchronized output; must alternate h, l, h, ...
 *   UTF-8 text      graphemes classified by the caller (screen.h)
 *
 * Wrapping follows R33: autowrap is modeled on with deferred wrap. A glyph
 * that lands in the last column leaves the cursor there in the pending-wrap
 * state; printing another glyph in that state fails (whatever DECAWM says),
 * as does a wide glyph that starts in the last column. No C0 control is
 * accepted, so nothing can scroll. The cursor is unknown until the first
 * CUP, and printing while it is unknown fails.
 *
 * Each vt_replay_feed call is one write: an escape sequence, a code
 * point, or a grapheme may not continue into the next call. The first
 * error is sticky: later feeds fail without changing anything.
 */
#ifndef OVERDRIVE_TEST_VT_REPLAY_H
#define OVERDRIVE_TEST_VT_REPLAY_H

#include "screen.h"

typedef struct VtReplay VtReplay;

/* A cols x rows blank screen, cursor unknown, cursor visible, autowrap on,
 * primary screen, no synchronized update open. Returns nullptr for a
 * nullptr classify, an invalid size, or an allocation failure. */
[[nodiscard]] VtReplay *vt_replay_create(int32_t cols, int32_t rows, TestClassify classify);

void vt_replay_destroy(VtReplay *vt);

/* Applies length bytes. Returns false on the first byte outside the
 * accepted protocol; vt_replay_error then names its offset (counted
 * across all feeds) and the offending bytes. */
[[nodiscard]] bool vt_replay_feed(VtReplay *vt, const void *bytes, size_t length);

/* The first error, or "" if none. */
const char *vt_replay_error(const VtReplay *vt);

const TestScreen *vt_replay_screen(const VtReplay *vt);

typedef struct VtReplayState {
    bool cursor_known;
    int32_t x;          /* 0-based column, meaningful when cursor_known */
    int32_t y;          /* 0-based row, meaningful when cursor_known */
    bool pending_wrap;  /* a glyph landed in the last column */
    bool cursor_visible;
    bool autowrap;
    bool alternate_screen;
    bool synchronized;  /* a ?2026h is open */
    OvdStyle style;     /* current SGR state */
} VtReplayState;

VtReplayState vt_replay_state(const VtReplay *vt);

#endif
