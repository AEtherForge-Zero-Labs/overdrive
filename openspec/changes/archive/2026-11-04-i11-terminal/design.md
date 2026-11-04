## Context

`include/overdrive/terminal.h` and the frame half of `render.h` fix the
public contract. The research entries cover the rest:
- R21: unchanged on failure;
- R22: invalid arguments;
- R23: one canvas, closed between frames;
- R24: sink re-entrancy;
- R25: sizes;
- R31: allocate before release;
- R35: how headless differs.

The pieces are merged:
- `ovd_alloc_all` (R39);
- `ovd_buffer_*`, plus `ovd_canvas_open` and `_close` (R42);
- `ovd_output_*`;
- `ovd_diff_render` and `ovd_diff_bound` (R43).

This change composes them into the frame state machine.

## Goals / Non-Goals

**Goals:**
- the headless backend, exactly as terminal.h and render.h describe it;
- a frame state machine whose every failure but `ovd_frame_end`'s
  `OVD_ERROR_IO` changes nothing;
- one sink call per non-empty frame;
- no allocation outside create and a resizing begin;
- no process-wide state (FR-002).

**Non-Goals:**
- the system backend: modes, enter and exit sequences, size re-reads,
  and color detection (OVD-11);
- the steady-state allocation test (OVD-13);
- boxes (OVD-09).

## Decisions

- **`struct OvdTerminal` lives in `src/internal.h`.** That is the plan's
  module table, and only `terminal.c` touches its fields. It holds:
  - the copied allocator;
  - the backend, depth, synchronized flag, sink, and sink context;
  - the current size, plus a pending size and a flag saying one is set;
  - two grids and an index naming the one the screen shows;
  - the output buffer;
  - `screen_known`;
  - the embedded canvas (R23);
  - `frame_open` and `sink_running`.

  The terminal itself is a single allocator block, so its address, and
  the canvas inside it, never change.
- **Create allocates everything in one `ovd_alloc_all` batch:** the
  terminal, two grids, and the output at `ovd_diff_bound` for the size.
  Any failure takes nothing.
  - An overflow in a grid request or in the bound is
    `OVD_ERROR_ALLOCATION`, as terminal.h says.
  - The screen starts unknown, so the first frame is a full redraw
    (FR-025).
- **Resize follows R31.** It happens only in a begin whose target size
  differs from the current one; headless has a pending size and no
  re-read.
  - One batch allocates two grids and an output. If it is refused,
    begin returns `OVD_ERROR_ALLOCATION` and changes nothing; the
    pending size stays (R21).
  - Otherwise each owner adopts its new block, which releases the old
    one. The size becomes the target, the pending flag clears, and the
    screen becomes unknown.
  - A pending size equal to the current one clears the flag and
    allocates nothing.
- **Begin, then end:**
  - **Begin** opens the canvas on the grid that is not the screen. That
    clears it and sets the full clip.
  - **End** closes the canvas first (R24), then empties the output. It
    renders with `screen` set to the screen grid if known, else nullptr.
    It calls the sink only if bytes were produced: at most once per
    frame, with `length > 0`, and with `sink_running` set (FR-022, R24).
  - **On success** the frame grid becomes the screen. On a sink failure,
    or on a render failure (which would be a bug, R30), the frame is
    still closed, the screen becomes unknown, and end returns
    `OVD_ERROR_IO`.
  - A nullptr sink discards output and counts as delivered.
- **Busy rules (R24):**
  - While `sink_running` is set, begin, end, and set_size return
    `OVD_ERROR_STATE`, and abandon and invalidate do nothing.
  - Size and depth return their usual values.
  - Calls on the canvas see it closed, because end closed it first.
- **Check order:** argument errors (`OVD_ERROR_INVALID_ARGUMENT`) come
  first, then the backend (`OVD_ERROR_UNSUPPORTED` from set_size), then
  state (`OVD_ERROR_STATE`). A call with several faults reports the first
  of these. This is the narrowest reading of terminal.h's lists.
- **The system backend is declined for now (R44).**
  - `ovd_terminal_create` validates the whole config first, as for
    headless, then returns `OVD_ERROR_UNSUPPORTED`. That avoids a
    half-made system backend (Art. V, headless first).
  - OVD-11 owns only `src/terminal_system.c`. To wire it in, it must
    change that one dispatch site in `terminal.c` and add its fields to
    `internal.h`. That needs are noted on #12. It would be an amendment
    to its owned paths, not something this change does ahead of need
    (Art. VII).
  - *Rejected:* calling not-yet-defined system functions, which would not
    link. *Rejected:* a stub `terminal_system.c`, which is #12's file.
- **Config defaults:**
  - `OVD_BACKEND_SYSTEM`, which is also what a zero-initialized config
    gives;
  - `detect_color_depth` true;
  - TRUECOLOR;
  - `alternate_screen` true;
  - `synchronized_output` false;
  - 80×24;
  - no sink, no allocator.

  Everything except the backend is stated in terminal.h or R35. The
  backend default is an assumption (R44).
- **Nullptr handling for the infallible functions (R22):**
  - `ovd_terminal_size(nullptr)` is `{0, 0}`.
  - `ovd_terminal_color_depth(nullptr)` is `OVD_COLOR_DEPTH_NONE`.
  - `destroy`, `abandon`, and `invalidate` given nullptr do nothing.
- **Setting the current size as the pending size** stores it. The next
  begin then sees no change, so there is no full redraw. "As a resize
  would" applies only when the size differs.
- **Tests:**
  - They use the public API only. `test_alloc.h`'s counting and failing
    allocator is the seam for allocation failures and counts.
  - The sink records each call's bytes for checking, and one variant
    re-enters the API from inside the sink.
  - The test links the library as a consumer and compiles no library
    source, so the Windows dllimport issue from #38 cannot arise.
    `support/test_alloc.c` is compiled in.

## Risks / Trade-offs

- OVD-11 has to amend its owned paths to wire the system backend in.
  That is the price of not shipping a stub in someone else's file.
- Resizing allocates three blocks, and the peak during a resize is old
  plus new storage. R31 asks for exactly this.

## Assumptions (R44, labeled `assumption`)

These are the decisions above that terminal.h and the research leave
open:
- the system backend returns `OVD_ERROR_UNSUPPORTED` until OVD-11;
- the default backend is system;
- the check order;
- a same-size pending size does not force a full redraw;
- a nullptr sink counts as delivered.
