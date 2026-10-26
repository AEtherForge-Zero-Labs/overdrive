# Research and Decisions: 001-render-core

Each decision lists what was chosen, why, and what was rejected. `[OPEN]`
marks a decision that needs the owner before the dependent issue starts.

## A. Analysis: OVD-IDENTITY-001 against the constitution

| Topic | Identity spec | Constitution (v1.3.0) | Resolution |
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
Unicode version: the latest released version at implementation time,
recorded in `cmake/` and here (assumption, §D.4). Terminals
disagree on emoji and ambiguous widths; ambiguous (A) is narrow. The
exact grapheme, width, and replacement rules are R15–R17.

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
full; wide glyphs advance two columns. Output never relies on autowrap
(R33); framing is R34. One output buffer per terminal, sized to a proven
worst-case bound at each size and reused (R30); one write per frame
(FR-022).

**R9 — Color downsampling.** RGB → 256: nearest of the 6×6×6 cube and the
24-step gray ramp by squared distance in sRGB. RGB/256 → 16: nearest of
the xterm default 16-color values. Indexed 0–15 are passed through at
depth 16+ (they follow the user's theme). Depth NONE: attributes only.
*Rejected:* perceptual color spaces — no consumer need; revisit with a
measured complaint.

**R10 — Color detection.** `NO_COLOR` non-empty → NONE.
`COLORTERM` = `truecolor`/`24bit` → TRUECOLOR. Windows with VT enabled →
TRUECOLOR. `TERM` contains `256color` → 256. `TERM` = `dumb` → NONE.
Otherwise 16. Config override wins over all. System backend only; first
match wins (R35, R36).

**R11 — Platform layer.** One private header `src/platform/platform.h`
with plain functions (no function-pointer table: one platform per build,
selected by CMake). Files: `platform_posix.c`, `platform_windows.c`.
System backend changes and restores: (Windows) output mode
`ENABLE_VIRTUAL_TERMINAL_PROCESSING`, `DISABLE_NEWLINE_AUTO_RETURN`,
output code page 65001, input echo; (POSIX) `ECHO` in termios, not
`ICANON` (R27); (both) alternate screen, cursor visibility, autowrap.
Order, rollback, and what "restored" means: R26, R28. Size:
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

**R14 — Branching.** `main` only, protected; each issue's tx transaction
branch merges through one PR (constitution Art. XVI). *Rejected:*
`develop`/`main` split — nothing to stage yet.

## D. Owner decisions (2026-10-26)

1. Constitution ratified as v1.3.0, with agent merge.
2. Merge authority: agents merge unless a human is needed (Art. XVI).
3. License: undecided; blocks publication only. Repository private.
4. Unicode version pin (R5): delegated as a documented assumption to OVD-03.
5. Repository: `AEtherForge-Zero-Labs/overdrive`, owner `@NeonSupernova`.
6. claude-automata adopted as the autonomy harness, alongside Spec Kit.
7. tx transactions carry issue work; OpenSpec records each transaction,
   Spec Kit keeps requirements (constitution v1.4.0).
8. Contract contradictions resolved as R15–R36 (#20, labeled
   `assumption`). tx slugs must start with a letter, so issue
   transactions use `i<number>-<slug>` (assumption pending a constitution
   PATCH; v1.4.0 says `<issue-number>-<slug>`).

## E. Contract resolutions (issue #20)

The spec, R1–R14, and the proposed headers contradicted one another or left
behavior undefined. Each decision below takes the narrowest reading that
keeps every requirement satisfiable, and is reflected in `spec.md`, the
headers in `contracts/`, and `tasks.md`. Labeled `assumption` for the
owner's after-the-fact review (Art. IX).

### Unicode

**R15 — Grapheme rule.** A grapheme is one *base* code point followed by
zero or more *extenders*. Extenders: General_Category Mn and Me, U+200D
ZWJ, variation selectors (U+FE00–U+FE0F, U+E0100–U+E01EF), and every other
Cf code point except the bidi controls of R17. The code point after a ZWJ
is also part of the grapheme unless R17 replaces it (then it starts a new
grapheme as U+FFFD), so a ZWJ emoji sequence is one grapheme
(spec Assumptions); R5 said "base + trailing zero-width code points",
which split it. Mc, emoji modifiers, and regional-indicator pairing are
not extenders (full UAX #29 stays out of scope). Each `ovd_canvas_text`
call segments its own text: an extender at the start is lone and becomes
U+FFFD; it never attaches to a cell already on the canvas.

**R16 — Grapheme width.** A grapheme's width is its base's: 2 if the
base's East_Asian_Width is W or F, otherwise 1 (A is narrow, R5).
Extenders, including VS16, never change it; terminals disagree there,
and this is the documented, testable choice. A replaced grapheme
(U+FFFD) has width 1, whatever it replaced.

**R17 — Replacement set.** Each of these becomes one U+FFFD, which is a
base like any other (following extenders attach to it):
- every maximal subpart of an ill-formed UTF-8 sequence (Unicode Standard,
  Chapter 3, "U+FFFD Substitution of Maximal Subparts"), including
  surrogates, overlongs, values above U+10FFFF, and a sequence truncated
  by `length`;
- C0 (U+0000–U+001F, including NUL, which does not terminate text), DEL,
  and C1 (U+0080–U+009F);
- U+2028 and U+2029;
- the bidi controls U+061C, U+200E, U+200F, U+202A–U+202E,
  U+2066–U+2069, which would let caller text reorder the terminal's
  display (Art. XIII);
- a grapheme longer than 15 UTF-8 bytes: the whole grapheme, base and
  extenders.

The Unicode version stays a documented assumption of OVD-03 (§D.4); it is
recorded once, in `cmake/gen_unicode_tables.cmake`, and cited here.

### Clipping

**R18 — Overflow-safe geometry.** Rect and coordinate arithmetic is done
in 64 bits; any `int32_t` input is valid. `ovd_canvas_set_clip` stores the
intersection with the canvas; an empty intersection is stored as
`{0, 0, 0, 0}`. `ovd_canvas_text` returns the summed width of every
grapheme in `text`, saturating at `INT32_MAX`.

**R19 — Cut wide glyphs.** If either half of a wide grapheme would fall
outside the clip or the canvas, its half that is inside (if any) is drawn
as a space in the call's style, and the outside half is untouched. This
covers the left edge, which the spec's edge case omitted. The text still
advances two columns.

**R20 — Pairing beats the clip.** The buffer never holds half of a wide
grapheme. When a draw overwrites one half of a wide grapheme whose other
half lies outside the clip, that other half becomes a space and keeps its
own style. It is the only write outside the clip (FR-030 amended).
*Rejected:* refusing the draw (the caller's visible text silently
missing); leaving the orphan (the diff would emit half a glyph).

### Failure semantics

**R21 — Unchanged on failure, with one named exception.** FR-001 holds for
every function except `ovd_frame_end`, which closes the frame on every
outcome and, on `OVD_ERROR_IO`, marks the screen unknown. A failed
`ovd_frame_begin`, including `OVD_ERROR_ALLOCATION` on a resize, opens no
frame and changes nothing: the old buffers and old size stay, and the
next begin retries. That is what "keeps working at its old size" means.

**R22 — Invalid arguments.** Fallible functions return
`OVD_ERROR_INVALID_ARGUMENT` for nullptr objects or out pointers, for
non-positive sizes (`ovd_terminal_create` headless, `ovd_terminal_set_size`),
for out-of-range `OvdBackend`/`OvdColorDepth`, and for an allocator whose
functions are nullptr. Infallible functions given nullptr do nothing and
return zero values. Out-of-range values that can't fail are normalized:
an unknown `OvdColorKind` is the default color, unknown attribute bits
are dropped, and an unknown `OvdBorder` draws nothing. Cells store
colors with only their kind's fields set, so equal-looking colors compare
equal.

**R23 — Closed canvas.** After `ovd_frame_end` or `ovd_frame_abandon`,
the canvas pointer stays valid until the terminal is destroyed but is
closed: drawing does nothing, queries return zero values,
`ovd_canvas_cell` returns false. After `ovd_terminal_destroy`, using it
is undefined (it cannot be detected without process-wide state).

**R24 — Sink re-entrancy.** While the sink runs, the terminal is busy:
fallible calls on it return `OVD_ERROR_STATE`, infallible ones do
nothing, and calling `ovd_terminal_destroy` from the sink is undefined.

**R25 — Sizes.** A 1×1 terminal works. A system terminal whose size reads
as 0 in either dimension fails `ovd_frame_begin` (or `ovd_terminal_create`)
with `OVD_ERROR_PLATFORM`.

### Restoration

**R26 — What "restored" means.** Operating-system modes (POSIX termios;
Windows console input mode, output mode, and output code page) are read
before they change and restored to the values read. Terminal state that
can only be read back by querying the terminal (which needs input, out of
scope) is set on exit to its default: `CSI 0 m`, `CSI ? 7 h` (autowrap),
`CSI ? 25 h` (cursor visible), then `CSI ? 1049 l` if the alternate screen
was entered. Screen contents and scrollback are preserved only through the
alternate screen (the default). With `alternate_screen = false` the last
frame stays on screen and the cursor is left at column 0 of the last row.
Assumption: before create, the cursor was visible and autowrap on, which
is the normal shell state.

**R27 — Echo only.** The system backend turns off input echo and nothing
else on the input side: POSIX `ECHO`, Windows `ENABLE_ECHO_INPUT`. R11's
`ICANON` is dropped: no input is read, so nothing needs it (Art. VII);
the header already said echo only. Input modes change only if stdin is a
terminal; otherwise that step is skipped, and is not an error.

**R28 — Order, rollback, best effort.** Create, in order:
1. require stdout to be a terminal, writing nothing otherwise;
2. (Windows) enable virtual-terminal output and
   `DISABLE_NEWLINE_AUTO_RETURN` (failure → `OVD_ERROR_PLATFORM`; no
   pre-1809 fallback);
3. (Windows) set the output code page to 65001;
4. turn off input echo;
5. read the size;
6. allocate;
7. write the enter sequence (alternate screen if configured, hide the
   cursor, autowrap off) in one write.

Each completed step is recorded. A failure undoes the recorded steps in
reverse and returns the error. Destroy abandons any open frame, writes
the R26 exit sequence in one write, then undoes steps 4, 3, 2 in that
order, and frees. Every undo is attempted even if an earlier one failed;
destroy cannot report failure.

**R29 — Two system terminals.** No process-wide state (FR-002), so
nothing detects a second system terminal. Each restores what it saw at
its own create, so they must be destroyed in reverse order of creation,
and only one may render: their screen models would contradict each other.

### Allocation

**R30 — Output buffer sized to a proven bound.** At create and on resize,
the output buffer is allocated to the worst-case size of one frame's
output at that size. That bound is fixed overhead plus cells × (max cursor
move + max SGR + 15 glyph bytes), computed by the diff module from the
encodings it emits. `ovd_frame_end` never allocates, so it has no
`OVD_ERROR_ALLOCATION` and SC-004 holds for any content. The output module
refuses appends past capacity. If that ever happens (a bug), the frame
fails with `OVD_ERROR_IO`, nothing is written, and the next frame is a
full redraw. Tests drive adversarial frames against the bound.
*Rejected:* growing in frame end, which breaks SC-004 and needs an error
code the contract lacks; chunked writes, which break FR-022.

**R31 — Resize is allocate-then-release.** New screen, frame, and output
buffers are all allocated before the old ones are released, so a refusal
leaves the terminal intact (R21). Nothing reallocates, so
`OvdAllocator.reallocate` is removed (Art. VII).

**R32 — Allocator contract.** `allocate(context, size)` is never called
with size 0 and returns memory aligned for any object type, or nullptr.
`release(context, block, size)` gets the size that was requested and
never nullptr. Size computations are overflow-checked; overflow is
`OVD_ERROR_ALLOCATION` with no allocator call. Allocation happens only in
`ovd_terminal_create` and in an `ovd_frame_begin` whose size differs from
the current buffers; nothing else allocates.

### Protocol and backends

**R33 — Never rely on autowrap.** After a glyph lands in the last column
(the terminal's pending-wrap state), the next glyph is always preceded by
a cursor move. Output therefore renders the same with autowrap on or off,
and the bottom-right cell never scrolls, given deferred wrap (the VT100
last-column flag; xterm's control-sequence reference). The system backend
still turns autowrap off, as defense in depth. The replay interpreter
models deferred wrap with autowrap on and fails a test that prints in
pending-wrap state or scrolls. This replaces R8's "autowrap is off, so
the bottom-right cell is safe", which made FR-024 depend on autowrap.

**R34 — Frame output framing.**
- A full redraw starts with `CSI 0 m` and `CSI 2 J`, then writes only
  non-default cells.
- Every non-empty output ends with `CSI 0 m`.
- With `synchronized_output`, the output is wrapped in `CSI ? 2026 h` …
  `CSI ? 2026 l`.
- An unchanged frame writes nothing.
- "Single write" (FR-022) means one contiguous buffer: one sink call
  (headless), or one write loop with nothing interleaved (system).

**R35 — Headless differs in four named ways.** FR-012 is restated. The
headless backend differs from the system backend only in:
1. where output goes (the sink);
2. where the size comes from (config and `ovd_terminal_set_size`);
3. where the color depth comes from: `color_depth` as configured, never
   the environment, so snapshot tests are deterministic. Detection is
   system-only.
4. lifecycle: it changes no modes and emits no enter or exit sequences;
   the sink receives frame output only.

Defaults: `color_depth` TRUECOLOR, `headless_size` 80×24.

**R36 — Color detection precision.** R10's rules apply first-match in the
order listed. `NO_COLOR` counts when present and non-empty (no-color.org).
`COLORTERM` matches `truecolor` or `24bit` exactly.
