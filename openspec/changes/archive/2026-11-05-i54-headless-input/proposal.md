## Why

Issue #54 (OVD-17) makes input work on the headless backend. That means
`ovd_input_wait` and `ovd_input_feed` over the decoder OVD-16 added, and
the input queue and paste buffer allocated at create. Headless comes
before the system backend (Art. V). OVD-18 reuses the same wait loop
with real reads.

## What Changes

- `src/input.c` (new):
  - `ovd_input_feed` copies bytes into the fixed input queue
    (`OVD_INPUT_QUEUE_SIZE`, 4096 bytes) and reports how many it took
    (R110);
  - `ovd_input_wait` decodes the queued bytes into one event. Headless
    time is virtual: each wait's timeout counts as time passing, so a
    lone ESC resolves without sleeping (R109). Inside the sink, wait
    returns `OVD_ERROR_STATE` (R102). A size stored by
    `ovd_terminal_set_size` is reported once as `OVD_EVENT_KIND_RESIZE`
    (R110).
- `src/internal.h`, `src/terminal.c`:
  - `OvdTerminal` gains the input state (queue, paste chunk buffer,
    decoder, virtual clock, pending resize);
  - when `input` is on, create allocates the queue and the chunk buffer
    in its one batch (R114), and destroy frees them;
  - T103's interim `OVD_ERROR_UNSUPPORTED` remains for the system
    backend only, until OVD-18.
- Tests:
  - `tests/test_input.c` (new) covers T110 to T112;
  - `tests/test_steady_state.c` gains an input case with 10⁴ feed and
    wait calls and 0 allocator calls (T112);
  - in `tests/test_terminal.c`, the interim assertion now covers the
    system backend only. It is a direct consequence of T113 (design
    D4).
- Build: `src/input.c` is appended to the library's sources, and
  `test_input` is registered.
- The T110–T113 ticks in `specs/002-input-restore/tasks.md`, and
  research.md R119, which records design D1 to D3 and D5 (they shape
  observable behavior, CLAUDE.md "Ask vs. decide").

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

- Public API: `ovd_input_wait` and `ovd_input_feed`, declared at OVD-15,
  are now defined. No header changes.
- A headless terminal with input on now succeeds where it returned
  `OVD_ERROR_UNSUPPORTED`. A system terminal with input on still returns
  `OVD_ERROR_UNSUPPORTED` until OVD-18.
- No platform code changes, and no signal handler is installed
  (FR-035).
