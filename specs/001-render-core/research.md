# Research and Decisions: 001-render-core

Each decision lists what was chosen, why, and what was rejected. `[OPEN]`
marks a decision that needs the owner before the dependent issue starts.

## A. Analysis: OVD-IDENTITY-001 against the constitution

| Topic | Identity spec | Constitution (v1.2.0 proposed) | Resolution |
|---|---|---|---|
| Module list (§4) | Eight modules named | Art. VII: no speculative abstraction | Modules are vocabulary. This feature creates Core, Render, Canvas, Platform only; Art. VII states this explicitly. |
| Example headers (§3.5) | `render.h`, `layout.h`, `motion.h` | Art. VII: a header needs a present requirement | Ship `core.h`, `terminal.h`, `render.h`, umbrella `overdrive.h`. No `layout.h`/`motion.h` until those features. |
| `OvdFrame` example type (§3.2) | Listed | Art. VII | Not created: a frame is a state of the terminal, and `ovd_frame_begin` hands out the canvas. Names are examples per §3.1. |
| Build options (§5.4) | Four examples | Art. VII | Only `OVERDRIVE_BUILD_TESTS` and `OVERDRIVE_BUILD_EXAMPLES` (needed by this feature). `OVERDRIVE_BUILD_BENCHMARKS` arrives with US4. No `OVERDRIVE_ENABLE_SIXEL`. |
| Testability (§4.2) | Render testable without a terminal | Art. V: mechanism before interface | Headless backend is public and first in build order. |
| Claims (§7.2) | Evidence required | Art. XV | Adopted as an article; SC targets are labeled as targets. |
| Transparent structs (§3.2) | Only when intentional and ABI-safe | Art. IV / XIV plan gate | `OvdColor`, `OvdStyle`, `OvdSize`, `OvdRect`, `OvdVersion`, `OvdCellView`, `OvdTerminalConfig`, `OvdAllocator` are transparent with stated reasons (plan §Public API). |
| Flag sets | `Ovd` enums | Art. III: distinct types | **Tension.** C cannot type a set of enum bits. Resolution: `OvdAttr` enum for the values, `uint16_t attrs` for the set, documented at the field. A single-member struct was rejected: it makes every `a \| b` a function call for no checked benefit. |
| Coordinates | — | Art. III: values that would be a bug to mix | **Tension.** Columns vs rows. Resolution: named fields (`cols`, `rows`, `x`, `y`) in plain `int32_t`; distinct types rejected as friction without a demonstrated bug class. Revisit if one appears. |
| Positioning (§1.5, §5.5) | Task app is the first consumer | Art. XIV: consumer independence | Nothing app-specific in the API. App integration is a separate decision in the app's repository after feature 002 (input). |
| GitLab references (§1.5, §10) | GitLab, classroom governance | Lab GitHub process (Art. XVI) | Recorded as context notes in the adoption record; spec text unedited. |
| "Open-source" (header) | Open-source | Lab property; license undecided | `[OPEN]` License choice. All rights reserved until decided. |

## B. Consumer evidence (task-tracking application)

The first consumer already contains, in its own library: a platform
terminal layer (Windows console + POSIX termios; raw mode, key decoding,
width query) and a streaming UI that writes styled text and boxes
directly, with a writer callback for tests. What this tells the design:

- A writer-callback test seam is already proven useful → Overdrive's
  headless sink (Art. VII justification for the `OvdSink` function
  pointer).
- Boxes and styled table cells are present needs → US3.
- Input is a present need of the consumer → feature 002, not 001.
- Nothing is copied from the app: different ownership, and different
  model (cell grid + diff vs. streamed text).

## C. Technical decisions

**R1 — Immediate-mode frames.** Each frame starts blank and describes the
whole screen; diffing against the screen makes redraw cheap on the wire.
*Rejected:* retained frames (start from previous contents) — saves
caller work but makes "a frame is a coherent state" (§2.3) depend on
history.

**R2 — Opaque terminal and canvas; canvas owned by the terminal.** No
allocation per frame; only one frame can be open. *Rejected:* separate
`OvdFrame` object (Art. VII); off-screen canvases (no consumer yet).

**R3 — Headless backend in the public API.** Consumers need snapshot
tests too, not just Overdrive. *Rejected:* test-only internal renderer
API (consumers couldn't test).

**R4 — Allocator hook.** Needed now for two things: SC-004 (count
allocations in steady state) and allocation-failure tests on every
platform. *Rejected:* `--wrap=malloc` (GNU ld only; the consumer uses it
and is Linux-only for that test).

**R5 — Unicode measurement.** Width from East Asian Width (W/F → 2),
zero width for Mn/Me/Cf combining-class code points, ZWJ, and variation
selectors; graphemes = base + trailing zero-width code points, ≤ 15 bytes.
Lone combining mark → U+FFFD. Tables generated at build time by a
`cmake -P` script from vendored UCD files (Art. II: generated output is
never committed; Art. VIII: CMake is already declared, no new tool).
`[OPEN]` Unicode version to pin — proposal: the latest released version
at implementation time, recorded in `cmake/` and docs. Terminals
disagree on emoji and ambiguous widths; ambiguous (A) is narrow.

**R6 — Control-character policy.** C0, DEL, C1 → U+FFFD at the canvas
boundary (Art. XIII). Tabs are not expanded. Text is single-line.

**R7 — Output protocol subset.** CUP (`CSI row;col H`), SGR (reset, 1, 2,
3, 4, 7, 9, 30–37/90–97, 38;5, 38;2, 40–47/100–107, 48;5, 48;2, 39, 49),
ED 2, DECTCEM (`?25`), DECAWM (`?7`), alternate screen (`?1049`),
optional synchronized output (`?2026`). Nothing else is emitted. The
test-only replay interpreter implements exactly this subset.

**R8 — Diff strategy.** Row by row: find runs of changed cells; move with
CUP only when the cursor is not already there; emit SGR only on style
change, as a minimal delta when attributes are only added, else reset +
full; wide glyphs advance two columns. Autowrap is off for the session,
so the bottom-right cell is safe (FR-024). One output buffer per terminal,
grown geometrically, reused; one write per frame (FR-022).

**R9 — Color downsampling.** RGB → 256: nearest of the 6×6×6 cube and the
24-step gray ramp by squared distance in sRGB. RGB/256 → 16: nearest of
the xterm default 16-color values. Indexed 0–15 are passed through at
depth 16+ (they follow the user's theme). Depth NONE: attributes only.
*Rejected:* perceptual color spaces — no consumer need; revisit with a
measured complaint.

**R10 — Color detection.** `NO_COLOR` non-empty → NONE.
`COLORTERM` = `truecolor`/`24bit` → TRUECOLOR. Windows with VT enabled →
TRUECOLOR. `TERM` contains `256color` → 256. `TERM` = `dumb` → NONE.
Otherwise 16. Config override wins over all.

**R11 — Platform layer.** One private header `src/platform/platform.h`
with plain functions (no function-pointer table: one platform per build,
selected by CMake). Files: `platform_posix.c`, `platform_windows.c`.
System backend changes and restores: (Windows) output mode
`ENABLE_VIRTUAL_TERMINAL_PROCESSING`, `DISABLE_NEWLINE_AUTO_RETURN`,
output code page 65001, input echo; (POSIX) `ECHO` and `ICANON` in
termios; (both) alternate screen, cursor visibility, autowrap. Size:
`GetConsoleScreenBufferInfo` window rect / `ioctl(TIOCGWINSZ)`. Writes
loop on partial writes and `EINTR`. Exact API behavior is to be verified
against Microsoft and POSIX documentation during the platform issue
(Art. X).

**R12 — Symbol visibility and the prefix check.** Static and shared builds
(`BUILD_SHARED_LIBS`); `C_VISIBILITY_PRESET hidden`; `OVD_API` on public
declarations. A `cmake -P` check lists exported symbols (`nm` on
ELF/Mach-O; `objdump -p` export table on PE) and fails on any without
`ovd_` (FR-040). `nm`/`objdump` come with the declared toolchains.

**R13 — Tests.** Plain C test executables registered with CTest, no
framework (Art. VII). Expected screens written as text rows (Art. VI).
Replay interpreter in `tests/support/`.

**R14 — Branching.** `main` only, protected; issue branches merge through
PRs. *Rejected:* `develop`/`main` split — nothing to stage yet.

## D. Open decisions for the owner

1. Ratify constitution v1.2.0 (or amend first).
2. Merge authority for the agent swarm: keep human merge (Art. XVI as
   drafted) or amend to allow agents to merge when all required checks
   are green and a reviewer agent approves.
3. License (blocks publication, not implementation).
4. Unicode version pin (R5) — default: latest release at implementation.
5. Confirm `@NeonSupernova` as CODEOWNER and GitHub repository
   `AEtherForge-Zero-Labs/overdrive`.
