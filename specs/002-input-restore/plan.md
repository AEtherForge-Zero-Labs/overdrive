# Implementation Plan: Input and Abnormal-Exit Restoration

**Branch**: `002-input-restore` | **Date**: 2026-11-05 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/002-input-restore/spec.md`

## Summary

Add input to the existing terminal: opt-in raw mode at create, a
bounded decoder from terminal bytes to typed events (keys, SGR mouse,
bracketed paste, focus, resize), and one wait call with a timeout. Add
abnormal-exit restoration for system terminals: process-wide signal and
console-control handlers, on by default, that replay destroy's restore
once and then let the process end as it would have. Headless first
(Art. V): decoder → headless feed/wait → POSIX raw mode and wait →
POSIX restoration → Windows → example and checklist. Decisions are in
[research.md](research.md) (R101–R117); the public contract is
[contracts/](contracts/include/overdrive/).

## Technical Context

**Language/Version**: ISO C23, as 001
**Primary Dependencies**: C standard library; POSIX termios, `poll`,
`pipe`, `sigaction`; Win32 console input (`ReadConsoleInputW`,
`SetConsoleCtrlHandler`, `SetUnhandledExceptionFilter`). No new library.
**Storage**: N/A
**Testing**: CTest; table-driven headless decode tests; seeded fuzz
under a new `sanitize` preset; POSIX pseudo-terminal tests (the
`test_system_pty` harness from #45); manual checklist
**Target Platform**: as 001 (Windows 10 1809+ with Windows Terminal
primary, Linux, macOS)
**Project Type**: library plus examples
**Performance Goals**: SC-004 (0 allocations reading input), SC-005 (no
waiting when an event is available). No latency target before a
measurement (Art. XV).
**Constraints**: no allocation after create; bounded decoding of
untrusted bytes; only async-signal-safe calls in POSIX handlers;
process-wide state only as spec FR-036 allows
**Scale/Scope**: ~2–3k lines of C including tests

## Constitution Check

*Gate against constitution v1.4.2. Checked before research and again
after the design below.*

| Article | Gate | Status |
|---|---|---|
| I Contracts are types | Every boundary has a contract form | PASS — public: `input.h`, `OvdTerminalConfig` fields; internal: `decode.h`, `platform.h` additions; runtime-validated: decoder bounds and progress (data-model), the once-only restore claim, paste UTF-8 validity; external-spec-derived: xterm sequences (R105, R116), POSIX termios/signals (R106, R112), Win32 console (R107, R113) |
| II Generated code separate | — | PASS — nothing generated |
| III Strict typing | Closed sets are fixed-type enums | PASS with one named tension (modifier set, research §C) |
| IV Ownership | Paradigm per module | PASS — table below; `OvdEvent` transparency named |
| V Mechanic before interface | Order | PASS — decoder and headless before platform; example last |
| VI Human-readable | Readable test expectations | PASS — decode table in notation, not byte dumps (R117) |
| VII No speculation | Each piece justified | PASS — table below; kitty/sixel get nothing (R108) |
| VIII Clean checkout | Declared tools | PASS — `sanitize` preset declared in `CMakePresets.json`, CI job added, sanitizer runtimes added to `.specify/memory/baseline.md` (T104) |
| IX Agent discipline | Assumptions recorded | PASS — research tags each ASSUMED item; the issues verify them |
| X Evidence | Unverifiable areas named | PASS — Windows behavior is ASSUMED until the checklist; SC-006 is human-run |
| XIII Security | Terminal input untrusted | PASS — FR-014, FR-019, SC-002 fuzz; paste repaired to UTF-8 |
| XIV Identity/API | Every public symbol listed | PASS — table below |
| XV Claims | No unmeasured claims | PASS — 50 ms escape delay is a default, not a claim |
| XVI Process | Issues, gated merge | PASS — `tasks.md` → issues after owner approval |

**Tension recorded:** 001 FR-002 (no process-wide state) vs spec FR-035
and FR-036. Resolution: one static restoration record and the POSIX
self-pipe, only for system terminals with the option or input on,
documented at the config fields (research §C).

## Public API (Art. XIV plan gate)

| Header | Symbols |
|---|---|
| `input.h` (new) | `OVD_WAIT_FOREVER`, `OvdEventKind` + `OVD_EVENT_KIND_*`, `OvdKey` + `OVD_KEY_*`, `OvdModifier` + `OVD_MODIFIER_*`, `OvdKeyEvent`, `OvdMouseAction` + `OVD_MOUSE_ACTION_*`, `OvdMouseButton` + `OVD_MOUSE_BUTTON_*` (including `OVD_MOUSE_BUTTON_WHEEL_UP`/`_DOWN`), `OvdMouseEvent`, `OvdPasteText`, `OvdEvent`, `ovd_input_wait`, `ovd_input_feed` |
| `terminal.h` (changed) | `OvdTerminalConfig` gains `input`, `mouse`, `bracketed_paste`, `focus_events`, `escape_delay_ms`, `restore_on_exit`; `ovd_terminal_config_default` sets their defaults |
| `overdrive.h` | includes `input.h` |

Transparent structs and why: `OvdEvent` — a value returned once per
event and read by `switch (kind)`; accessors would be one call per field
for no invariant beyond the tag. `OvdKeyEvent`, `OvdMouseEvent` — plain
values. `OvdPasteText` — a view (pointer, length) into terminal-owned
memory, like `OvdCellView`; its lifetime is documented at the type.

## Modules (Art. IV / VII plan gates)

| File | Paradigm | Present requirement |
|---|---|---|
| `include/overdrive/input.h` | public header | FR-010, FR-011 (Input module, identity §4.7) |
| `src/decode.c` + `src/decode.h` | owning module (`OvdDecoder`: partial-sequence state) | FR-011–FR-016, FR-019; state outlives a call (split sequences) |
| `src/input.c` | part of the terminal module: `ovd_input_wait`, `ovd_input_feed`, queue and paste buffers | FR-004, FR-010, FR-017, FR-018 |
| `src/terminal.c`, `src/internal.h` (changed) | terminal module | FR-001, R115 validation, buffers at create |
| `src/terminal_system.c` (changed) | terminal module, system backend | FR-002, FR-003, R116 sequences, exit sequence for the record |
| `src/platform/platform.h`, `platform_posix.c`, `platform_windows.c` (changed) | transformational OS wrappers plus the one process-wide restoration record | FR-002, FR-030–FR-036 (R106, R107, R111–R113) |
| `src/unicode.c` (reused; exposes its UTF-8 decoder, R118) | transformational | R103 paste repair, key UTF-8 decode (Art. IX: reuse, not duplicate) |
| `CMakePresets.json` `sanitize` preset + CI job; sanitizer runtimes declared in `.specify/memory/baseline.md` (Art. VIII rule 1) | declared check | SC-002 |
| `tests/test_decode.c`, `test_decode_fuzz.c`, `test_input.c`, `test_restore_pty.c`; `test_steady_state.c`, `test_system_pty.c` extended | tests | SC-001–SC-005 |
| `examples/input_demo.c`, `docs/validation/manual-checklist.md` | example, checklist | SC-006, US2 |

Deferred (no present requirement): hover tracking, enhanced keyboard
protocols, capability queries, graphics (kitty, sixel), suspend and
resume, cross-thread wakeup.

## Terminal modes (CLAUDE.md, Repo specifics)

Every mode create changes, and where it is undone. 001's rows are
unchanged and listed for completeness.

| Mode | Set at create | Undone at |
|---|---|---|
| Win output mode (VT, no auto return, processed) | step 2 (001) | destroy step, signal/console handler |
| Win output code page 65001 | step 3 (001) | destroy, handler |
| Input echo off | step 4 (001) | destroy, handler |
| Raw input (POSIX `ICANON ISIG IEXTEN IXON ICRNL INLCR IGNCR ISTRIP`, `VMIN/VTIME`; Win line/processed input off, VT/window input on, quick edit off with mouse) | new step 4b, after echo, only with `input` | destroy (before echo), handler |
| Alternate screen, cursor hidden, autowrap off | enter sequence (001) | exit sequence, handler |
| Mouse 1002 + 1006, paste 2004, focus 1004 | enter sequence, each only if on | exit sequence (reverse order, only those on), handler |
| POSIX self-pipe, `SIGWINCH` handler | new step 4c, after raw input, with `input`; process-wide, shared by every system terminal with input on | destroy (before raw input): the last user puts the previous action back if the current one is still Overdrive's, then closes the pipe |
| Signal / console-control handlers, exception filter | last step, with `restore_on_exit`; handled signals blocked in the calling thread for all of create | destroy, after its restore: each slot still holding Overdrive's handler gets the previous one back, then the record is disarmed (R112) |

A failed create undoes every completed step in reverse (001 FR-011).

## Project Structure

### Documentation (this feature)

```text
specs/002-input-restore/
├── spec.md
├── research.md
├── plan.md                 # this file
├── data-model.md
├── quickstart.md
├── contracts/include/overdrive/{input,terminal,overdrive}.h
├── checklists/requirements.md
├── tasks.md                # speckit-tasks
└── workgraph.json          # machine-readable issue graph for agents (OVD-15–OVD-21)
```

### Source Code (repository root)

```text
include/overdrive/  input.h (new)  terminal.h  overdrive.h
src/  decode.{c,h} (new)  input.c (new)  terminal.c  terminal_system.c  internal.h
src/platform/  platform.h  platform_posix.c  platform_windows.c
tests/  test_decode.c  test_decode_fuzz.c  test_input.c  test_restore_pty.c (new)
        test_steady_state.c  test_system_pty.c (extended)
examples/  input_demo.c (new)
docs/validation/  manual-checklist.md (extended)
CMakePresets.json  .github/workflows/ci.yml  (sanitize preset and job)
.specify/memory/baseline.md  (sanitizer runtimes)
```

**Structure Decision**: single library project, layout as in
`AGENTS.md`; no new directory.

## Workstreams (for speckit-tasks)

Seven issues, OVD-15 to OVD-21; [tasks.md](tasks.md) has the tasks and
[workgraph.json](workgraph.json) each issue's `owns` list:

1. **OVD-15 Contract freeze and sanitizer preset** — `include/overdrive/`
   copies, config defaults and validation, `sanitize` preset, CI rows,
   and the sanitizer runtimes in `.specify/memory/baseline.md`.
2. **OVD-16 Decoder** — `decode.{c,h}`, `test_decode.c`,
   `test_decode_fuzz.c`. After 1.
3. **OVD-17 Headless input** — `input.c`, terminal.c/internal.h
   buffers, `test_input.c`, `test_steady_state.c`. After 2.
4. **OVD-18 System input, POSIX and Windows** — platform files,
   `terminal_system.c` reports, `test_system_pty.c`. After 3.
5. **OVD-19 Restoration, POSIX and Windows** — platform files,
   `test_restore_pty.c`. After 4.
6. **OVD-20 Example and checklist** and **OVD-21 Docs**, in parallel.
   After 5.

The chain is linear through OVD-19. Each layer builds on the one before,
and `platform.h` declares functions both platform files must define, so
splitting POSIX from Windows would break the Windows cross build between
merges. Each platform layer therefore lands for both platforms at once.

## Complexity Tracking

| Item | Why needed | Simpler alternative rejected because |
|---|---|---|
| Process-wide signal handlers and record | FR-030, owner's default-on choice | App-installed handlers: every consumer writes the same unsafe code; owner declined |
| POSIX self-pipe | FR-017 resize while waiting | `ppoll` absent on macOS; timer polling wakes while idle (R111) |
| Generic CSI layer in the decoder | FR-014: unknown sequences must be consumed whole | Matching only known strings cannot tell where an unknown sequence ends |
| `sanitize` preset | SC-002 | Running the fuzz test unsanitized cannot show out-of-bounds reads |
