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
recorded once in `cmake/gen_unicode_tables.cmake` and cited here
(assumption, §D.4). Terminals
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
`ovd_` (FR-040). `nm`/`objdump` come with the declared toolchains. On a
static archive the listing has every global symbol, hidden or not, so
the rule the check enforces is: every non-static symbol in the library
carries `ovd_` (R41).

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
   `assumption`).
9. Issue transactions use `i<number>-<slug>` (constitution v1.4.2,
   Art. XVI).

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
not extenders (full UAX #29 stays out of scope). Two named deviations
from UAX #29 follow: there, most Cf code points (for example U+00AD and
U+200B) are Control and break, and Prepend marks (U+0600–U+0605) attach
to what follows; here every non-bidi Cf is an extender, so they attach to
the base before them. Unassigned code points are bases, with width from
`EastAsianWidth.txt` including its `@missing` defaults (W in the CJK
ideograph ranges). These UAX #29 and UCD properties are as recalled;
OVD-03 checks them against the pinned UCD. Each `ovd_canvas_text`
call segments its own text: an extender at the start is lone and becomes
U+FFFD; it never attaches to a cell already on the canvas. That U+FFFD is
a base, so extenders after it attach to it, and the cell's text is
U+FFFD followed by them (the 15-byte limit still applies): two leading
combining marks are one U+FFFD grapheme carrying the second mark. A ZWJ
that was replaced is no longer a ZWJ and joins nothing. A ZWJ sequence
longer than 15 bytes (a four-person family emoji is 25) becomes one
U+FFFD under R17.

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
  by `length`. `F0 80 80` and `ED A0 80` each give three U+FFFD (no
  valid sequence starts `F0 80` or `ED A0`); the truncated valid prefix
  `F0 9F 98` gives one. The count matters: it sets the column advance
  and `ovd_canvas_text`'s return value;
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

**Unicode version pin (OVD-03, #4; assumption, §D.4).** Unicode
**18.0.0**: the latest released version on 2026-10-26, when
`https://www.unicode.org/Public/UCD/latest/ReadMe.txt` named it final.
`third_party/ucd/` holds its `EastAsianWidth.txt` and
`extracted/DerivedGeneralCategory.txt` unchanged (headers dated
2026-06-29) with the Unicode License v3, and the generator rejects files
of any other version. R15's recalled properties, checked against these
files: U+200D, U+00AD, U+200B, U+0600–U+0605, U+FEFF, and every R17 bidi
control are Cf; U+FE00–U+FE0F and U+E0100–U+E01EF are Mn; so R15's
extenders are exactly Mn, Me, and Cf minus the bidi controls.
`EastAsianWidth.txt` has one `@missing` line (`0000..10FFFF; N`); its
header states the W defaults for U+3400–U+4DBF, U+4E00–U+9FFF,
U+F900–U+FAFF, and planes 2 and 3, and in 18.0.0 every such code point
is listed explicitly as W. The generator still applies `@missing` lines.
*Rejected:* 17.0.0 (superseded); a beta of a later version (not released).

### Clipping

**R18 — Overflow-safe geometry.** Rect and coordinate arithmetic is done
in 64 bits; any `int32_t` input is valid. `ovd_canvas_set_clip` stores the
intersection with the canvas; an empty intersection is stored as
`{0, 0, 0, 0}`. `ovd_canvas_text` returns the summed width of every
grapheme in `text`, saturating at `INT32_MAX`.

**R19 — Cut wide glyphs.** If either half of a wide grapheme would fall
outside the clip or the canvas, its half that is inside (if any) is drawn
as a space in the call's style, and the outside half is untouched except
for R20's repair. This covers the left edge, which the spec's edge case
omitted. The text still advances two columns.

**R20 — Pairing beats the clip.** The buffer never holds half of a wide
grapheme. When a draw overwrites one half of a wide grapheme whose other
half lies outside the clip, that other half becomes a space and keeps its
own style. It is the only write outside the clip (FR-030 amended).
*Rejected:* refusing the draw (the caller's visible text silently
missing); leaving the orphan (the diff would emit half a glyph).

### Failure semantics

**R21 — Unchanged on failure, with one named exception.** FR-001 holds for
every function and every error except `ovd_frame_end` returning
`OVD_ERROR_IO`: the frame is closed (as on `OVD_OK`) and the screen is
marked unknown. `ovd_frame_end`'s other errors (`OVD_ERROR_INVALID_ARGUMENT`,
`OVD_ERROR_STATE`) change nothing. A failed `ovd_frame_begin` changes
nothing: a frame that was already open stays open; otherwise no frame
opens, the old buffers and size stay, and the next begin retries. That
is what "keeps working at its old size" means. `ovd_terminal_size`
returns the size of the current buffers, or a size stored by
`ovd_terminal_set_size` that no begin has applied yet; a failed begin
changes neither, so a pending headless size survives it and is returned
until a begin applies it.

**R22 — Invalid arguments.** Fallible functions return
`OVD_ERROR_INVALID_ARGUMENT` for nullptr objects or out pointers, for
non-positive sizes (`ovd_terminal_create` headless, `ovd_terminal_set_size`),
for out-of-range `OvdBackend`/`OvdColorDepth` (`color_depth` is validated
even when detection will override it), and for an allocator whose
functions are nullptr. `headless_size` is validated only for the headless
backend. Infallible functions given nullptr do nothing and return zero
values; `ovd_canvas_text` then returns 0. Out-of-range values that
can't fail are normalized: an unknown `OvdColorKind` is stored, and
reads back, as `OVD_COLOR_KIND_DEFAULT`; unknown attribute bits are
dropped, and an unknown `OvdBorder` draws nothing. Cells store colors
with only their kind's fields set, so equal-looking colors compare
equal.

**R23 — One canvas per terminal, closed between frames.** The canvas
object is allocated once, at create, and `ovd_frame_begin` returns the same
pointer for the terminal's whole life; a resize swaps the cell buffers
behind it, never the object. So a pointer kept from an earlier frame is
simply the current canvas again once the next frame opens. Between frames
the canvas is closed: drawing does nothing, its queries return zero values,
`ovd_canvas_cell` returns false, `ovd_canvas_text` returns 0, and
`ovd_canvas_set_clip` stores nothing and returns `{0, 0, 0, 0}`. A
nullptr canvas behaves the same (R22). After
`ovd_terminal_destroy`, using it is undefined (detecting that would need
process-wide state).

**R24 — Sink re-entrancy.** `ovd_frame_end` closes the canvas before it
calls the sink, so every `ovd_canvas_*` call from inside the sink acts as
on a closed canvas (R23). While the
sink runs, the terminal is busy: fallible calls on it
(`ovd_frame_begin`, `ovd_frame_end`, `ovd_terminal_set_size`) return
`OVD_ERROR_STATE`; `ovd_frame_abandon` and `ovd_terminal_invalidate` do
nothing; the queries `ovd_terminal_size` and `ovd_terminal_color_depth`
return their normal values. Calling `ovd_terminal_destroy` from the sink is
undefined.

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
was entered, or else a cursor move to column 0 of the last row of the
last frame's size. If no frame has ended (destroy right after create,
or R28's rollback of a failed enter write), it is the last row of the
size read at create (R28 step 5). Screen contents and scrollback are
preserved only through the alternate screen (the default; xterm's mode
1049 saves the cursor, switches to the alternate screen, and restores
both on leave). With `alternate_screen = false` the last frame stays on
screen. Assumption: before create, the cursor was visible and autowrap
on, which is the normal shell state.

**R27 — Echo only.** The system backend turns off input echo and nothing
else on the input side: POSIX `ECHO`, Windows `ENABLE_ECHO_INPUT`. R11's
`ICANON` is dropped: no input is read, so nothing needs it (Art. VII);
the header already said echo only. Input modes change only if stdin is a
terminal; otherwise that step is skipped, and is not an error.

**R28 — Order, rollback, best effort.** Create, in order:
1. require stdout to be a terminal, writing nothing otherwise;
2. (Windows) enable `ENABLE_VIRTUAL_TERMINAL_PROCESSING` and
   `DISABLE_NEWLINE_AUTO_RETURN` on the output handle (failure →
   `OVD_ERROR_PLATFORM`; no pre-1809 fallback). Microsoft's SetConsoleMode
   documentation: the latter makes the console defer the wrap at the last
   column, as VT terminals do (R33);
3. (Windows) set the output code page to 65001;
4. turn off input echo;
5. read the size;
6. allocate;
7. write the enter sequence (alternate screen if configured, hide the
   cursor, autowrap off) in one write.

Each completed step is recorded. A failure undoes the recorded steps in
reverse and returns the error. If step 7's write fails, part of the enter
sequence may have reached the terminal, so the R26 exit sequence is
written best-effort before undoing steps 4, 3, 2, and create returns
`OVD_ERROR_IO`. That exit sequence counts the alternate screen as
entered whenever it was configured, so it includes `CSI ? 1049 l`: the
failed write does not show whether `CSI ? 1049 h` took effect. A
needless `CSI ? 1049 l` restores the cursor as DECRC does (the xterm
reference cited in R33), and DECRC with nothing saved moves the cursor
home (vt100.net/docs/vt510-rm/DECRC.html); a missing one would leave
the shell on the alternate screen. Both are cited, not observed.
Destroy abandons any open frame, writes the R26 exit sequence in one
write, then undoes steps 4, 3, 2 in that order, and frees. Every undo is
attempted even if an earlier one failed; destroy cannot report failure.

**R29 — Two system terminals.** No process-wide state (FR-002), so
nothing detects a second system terminal. Each restores what it saw at
its own create, so they must be destroyed in reverse order of creation,
and only one may render: their screen models would contradict each other.

### Allocation

**R30 — Output buffer sized to a proven bound.** At create and on resize,
the output buffer is allocated to the worst-case size of one frame's
output at that size. That bound is fixed overhead (synchronized-output
markers 16 bytes, full-redraw prefix 8, trailing reset 4) plus cells ×
(max cursor move + max SGR, with every attribute and two RGB colors + 15
glyph bytes), computed by the diff module from the encodings it emits.
`ovd_frame_end` never allocates, so it has no `OVD_ERROR_ALLOCATION` and
SC-004 holds for any content. The output module refuses appends past
capacity. If that ever happens (a bug), the frame
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

**R39 — Allocation dispatch and output buffer (OVD-04).** Assumptions
where R30–R32 are silent, labeled `assumption`. The issue body's "output
buffer grows geometrically" predates R30/R31 and is not implemented: the
buffer never grows.
- A zero element count or element size is `OVD_ERROR_INVALID_ARGUMENT`
  with no allocator call (a caller bug; `allocate` never sees 0).
- A batch checks every request's size before its first allocator call;
  on a refusal it releases what it took in reverse order. On any
  failure, argument errors included, every output block is empty.
- R31's allocate-then-release is a batch allocation of all new blocks,
  then each owner adopting its new block and releasing its old one. No
  callback interface (Art. VII).
- `OvdOutput` never allocates; it adopts a block whose size is its
  capacity. A failed append writes nothing; failing the frame is the
  terminal's decision (R30).
- `OvdOutput` is declared in the private header so the terminal embeds it
  without another allocation; only `src/output.c` touches its fields
  (Art. IV tension, resolved in the doc comment).

### Protocol and backends

**R33 — Never rely on autowrap.** After a glyph lands in the last column
(the terminal's pending-wrap state), the next glyph is always preceded by
a cursor move. Output therefore renders the same with autowrap on or off,
and the bottom-right cell never scrolls, given deferred wrap: the VT100
last-column flag, described in xterm's control-sequence reference
(invisible-island.net/xterm/ctlseqs/ctlseqs.html), and on Windows
`DISABLE_NEWLINE_AUTO_RETURN` (R28). The system backend
still turns autowrap off, as defense in depth. The replay interpreter
models deferred wrap with autowrap on and fails a test that prints in
pending-wrap state or scrolls. This replaces R8's "autowrap is off, so
the bottom-right cell is safe", which made FR-024 depend on autowrap.

**R34 — Frame output framing.** A non-empty output is, in this order:
1. `CSI ? 2026 h`, only with `synchronized_output`;
2. for a full redraw, `CSI 0 m` `CSI 2 J`;
3. the cell updates (a full redraw writes only non-default cells);
4. `CSI 0 m`;
5. `CSI ? 2026 l`, only with `synchronized_output`.

An unchanged frame writes nothing at all. Mode 2026 is the synchronized
output convention proposed for terminal emulators
(gist.github.com/christianparpart/d8a62cc1ab659194337d73e399004036).
That a terminal without it ignores `CSI ? 2026 h` and `l` is ASSUMED
(Art. X): the proposal does not say so, and detects support with DECRQM,
which needs input (out of scope).
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

**Unverified (Art. X).** The terminal behaviors §E relies on are taken from
the cited documentation, not observed: deferred wrap (R33), mode 1049
saving and restoring the screen (R26), terminals ignoring mode 2026 when
unsupported (R34), and the Windows console modes (R28). They are checked
by hand in real terminals through the manual checklist (OVD-12), and any
difference found becomes a spec change.

### Color (OVD-02)

**R37 — Color encoding choices.** R8 and R9 leave these open; each takes
the narrowest reading (issue #3, labeled `assumption`).
- Both styles are normalized (unknown kind → default, undefined attribute
  bits dropped) and downsampled for the depth before they are compared; a
  change the depth cannot show writes nothing.
- "Attributes only added" means the new set contains the old. Then the
  SGR is a delta: added attributes, then the foreground if changed, then
  the background if changed (39/49 return to default). Otherwise it is one
  sequence starting with parameter 0, then every attribute and every
  non-default color: ECMA-48 SGR applies parameters in order, so
  `CSI 0;1 m` equals `CSI 0 m CSI 1 m`. Removal needs the reset because
  22–29 are outside R7. Attributes go in ascending code order (1, 2, 3,
  4, 7, 9). The longest sequence is 48 bytes.
- Indexed 0–15 are written as 30–37/90–97 (40–47/100–107) at every color
  depth; indexed 16–255 as `38;5;n` at 256 and truecolor (never expanded
  to RGB); RGB as `38;2;r;g;b` only at truecolor.
- RGB → 256 considers indices 16–255 only (0–15 follow the theme, so
  their RGB is unknown): cube levels 0, 95, 135, 175, 215, 255 and gray
  8 + 10i. RGB and 16–255 → 16 use xterm's default color0–color15
  values (000000, cd0000, 00cd00, cdcd00, 0000ee, cd00cd, 00cdcd, e5e5e5,
  7f7f7f, ff0000, 00ff00, ffff00, 5c5cff, ff00ff, 00ffff, ffffff). Ties
  go to the lowest index. These values are as recalled from xterm's
  resources and are not observed in a terminal (Art. X).

### Replay oracle (OVD-05)

**R38 — The replay interpreter is stricter than any terminal.** R7, R33,
and R34 leave the oracle's edges open; it takes the strict side of each,
so output that depends on terminal-specific behavior fails a test
instead of passing by luck. Every rendering test inherits these rules:
- Widths come from a classifier the caller passes (code point →
  extender, narrow, wide). The oracle must not share the module it
  checks, so a rendering test passes an independent, hand-written
  classifier covering only the code points that test uses, applying
  R16 by hand; it never uses `src/unicode`, whose errors OVD-03's own
  tests catch. The oracle hard-codes only R15's ZWJ rule.
- R33: a glyph printed in the pending-wrap state fails whatever DECAWM
  says, and so does a wide glyph starting in the last column. No C0
  control is accepted, so nothing can scroll.
- The cursor is unknown until the first CUP and again after `?1049h` or
  `?1049l` (whether it survives the switch is terminal-dependent,
  ASSUMED); printing while it is unknown fails.
- Encodings are exact: every parameter is a decimal without leading
  zeros, with no digit limit, so every in-range coordinate parses; a
  value past `UINT32_MAX` is rejected, never wrapped into range. CUP has
  two explicit 1-based in-range parameters; reset is `CSI 0 m` (`CSI m` is
  rejected); ED is only `CSI 2 J` and only in the default SGR state,
  because the erase color is terminal-dependent; a DEC private mode
  carries one parameter; colon sub-parameters, attribute-off codes, and
  color values above 255 are rejected.
- SGR meanings follow ECMA-48 §8.3.117 and the xterm control-sequence
  reference (90–97 and 100–107 as palette 8–15, `38/48;5;n`,
  `38/48;2;r;g;b`), as recalled, not re-read (ASSUMED). Bold (1) and
  faint (2) are one intensity setting in ECMA-48 and terminals differ
  when both are set; the oracle keeps them as the independent `OvdAttr`
  bits the canvas holds, so it checks what was emitted, not what a
  terminal shows for that pair.
- Every escape sequence ends a grapheme, and so does the end of a
  write: an extender must follow its base directly in the same write.
  A grapheme may end in ZWJ (R15 allows `a` + ZWJ at the end of a call's
  text), and the oracle accepts that cell. Whether a terminal joins the
  ZWJ to the next glyph across an SGR or a write boundary is
  terminal-dependent, so the next glyph after such a cell must follow a
  CUP; only a CUP clears this, not ED 2 or `?1049`. In one run the code
  point after the ZWJ joins it, as in the canvas, so `a` ZWJ `b` written
  together is one grapheme for any R15 reader. The diff (OVD-08)
  therefore treats its cursor as unknown after such a cell, as after
  R33's pending wrap: this is the one case where R8's "only when the
  cursor is not already there" does not hold. That a CUP breaks a
  terminal's ZWJ join is ASSUMED, for the OVD-12 manual checklist.
- Overwriting one half of a wide glyph makes the other half
  indeterminate, never equal to anything, until it is overwritten:
  terminals disagree on what a split glyph shows.
- Mode 2026 must alternate `h`, `l`; its place in a frame (R34) is
  checked by the diff tests, not by the oracle. `?1049h` clears the
  model; `?1049l` makes every cell indeterminate (the primary screen is
  not modeled).
- Each feed is one write (FR-022): an escape sequence, code point, or
  grapheme may not span two feeds. Code points the canvas never holds
  (R17) are rejected. The first error is sticky and names its byte
  offset and the offending bytes; so is a feed of nullptr bytes with a
  nonzero length.

### Platform and detection (OVD-06)

**R40 — Platform-layer choices.** R10, R11, and R25–R28 leave these open.
Each takes the narrowest reading (issue #7, labeled `assumption`).
- `ovd_platform_enter` performs R28 steps 2–4 and undoes them itself on
  failure. Steps 1 and 5 are separate queries; steps 6 and 7 are the
  system backend's. The values read are kept in the caller's
  `OvdPlatformModes`, an aligned byte area that each platform file
  checks with `static_assert`, so no OS type reaches `platform.h` (R11).
  POSIX keeps the whole `struct termios`, because R26 restores "the
  values read".
- Windows step 2 also sets `ENABLE_PROCESSED_OUTPUT`: Microsoft's
  `SetConsoleMode` reference says, at
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING`, "Ensure ENABLE_PROCESSED_OUTPUT
  is set when using this flag". The flag is on by default, and leave
  restores the whole mode read at enter.
- POSIX `tcsetattr` "shall return successfully if it was able to perform
  any of the requested actions", so enter reads the modes back and
  fails, restoring them, if `ECHO` is still set. Both directions use
  `TCSANOW`: pending input is the user's, so it is not flushed.
- The size comes from `ioctl(TIOCGWINSZ)`, which is not POSIX (Linux
  man-pages `TIOCGWINSZ(2const)`; the macOS SDK's `sys/ttycom.h`).
  POSIX Issue 8's `tcgetwinsize` is the standard form, but the macOS SDK
  does not declare it.
- A write that accepts zero bytes for a non-zero request is a failure, on
  both platforms, so the loop cannot spin. Each call asks for at most
  `SSIZE_MAX` (POSIX) or `MAXDWORD` (Windows) bytes.
- R10's "Windows with VT enabled" comes from
  `ovd_platform_virtual_terminal`: true only if the Windows output-mode
  step is recorded. Detection stays platform-independent.
- INFERRED, not stated by Microsoft: `GetConsoleMode` fails for a
  redirected handle (the "is a terminal" test), and `srWindow`'s corners
  are inclusive (size = difference + 1). Both are on the OVD-12 manual
  checklist.

### Build records (OVD-01 review, #28)

**R41 — Clang 19 minimum, export rule, and CI facts.** Checked on
2026-11-02 and 2026-11-03; labeled `assumption`.
- Clang 18 lacks C23 object `constexpr`: Clang's C status page
  (clang.llvm.org/c_status.html) lists "constexpr for object
  definitions" (N3018) from Clang 19. Constitution Art. III asks for
  `constexpr` in named constants, and the baseline is subordinate to
  it, so the Clang minimum is raised to 19, in the baseline and in the
  configure check in `CMakeLists.txt`. CI's Clang job moves to
  `clang-19`, which Ubuntu 24.04 ships in universe (19.1.1,
  packages.ubuntu.com/noble/clang-19). Named constants that worked
  around Clang 18 with a macro or an enum can now be `constexpr`.
  *Rejected:* keeping Clang 18 and not using object `constexpr`, which
  would put `AGENTS.md` and new code against Art. III.
- Export rule (R12): the check runs on the static archive too, where
  hidden symbols are listed, so every non-static symbol carries `ovd_`.
  The `check_exports_static_rejects_generic` test pins it with a hidden
  generic name. *Rejected:* checking only the shared library, which lets
  a generic hidden name collide in a consumer's static link.
- `ovd_result_name` has no `default:`, so `-Wswitch` under `-Werror`
  fails the build for an `OvdResult` enumerator it does not name.
- CI builds with GCC 14 and Clang 19 but does not exercise the CMake or
  Apple Clang minimums. The last `library-ci` run on `main` before this
  change used CMake 3.31.6 on `ubuntu-24.04` and, on `macos-latest`
  (then `macos-26-arm64`), CMake 4.4.4 with Apple clang 21.0.0. Those
  minimums are checked by hand, per baseline.

### Grid and canvas (OVD-07)

**R42 — Grid and canvas choices.** R18–R23 and R31 leave these open.
Each takes the narrowest reading (issue #8, labeled `assumption`).
- The grid keeps pairing itself. Before a cell is overwritten, the other
  half of the wide grapheme it belongs to becomes a space in its own
  style. That one rule is R20, and it holds for any caller. A wide
  grapheme whose second column is off the grid is refused, and nothing
  changes. The canvas never writes outside the clip itself, so R20's
  repair is the only write outside it.
- The second half of a wide grapheme carries the first half's style. So
  "its own style" means the same thing from either side.
- Storage follows R39's pattern. `ovd_buffer_request` gives the request,
  the terminal batches it in `ovd_alloc_all`, and `ovd_buffer_adopt`
  releases the old block and clears the new one. A non-positive dimension
  is `OVD_ERROR_INVALID_ARGUMENT`. A `cols × rows` that overflows `size_t`
  is `OVD_ERROR_ALLOCATION`.
- `struct OvdCanvas` is a frame pointer and a clip. It is open exactly
  when the pointer is non-null, and zero-initialized means closed. It is
  declared in `src/buffer.h`, so the terminal can embed it (R23) without
  a separate `canvas.h`. `ovd_canvas_open` clears the frame buffer and
  sets the clip to the whole buffer: the canvas half of
  `ovd_frame_begin`.
- `ovd_canvas_cell` with a nullptr `out` returns false. That is R22's
  zero value for an infallible function.
- Styles are normalized once, when drawn (R22), and the grid stores what
  it is given.

### Differential renderer (OVD-08)

**R43 — Diff choices.** R8, R30, R33, and R34 leave these open. Each
takes the narrowest reading (issue #9, labeled `assumption`).
- `ovd_diff_render(screen, frame, depth, synchronized, output)` is
  transformational. `screen` nullptr means the screen is unknown, and
  the output is a full redraw (FR-025). The terminal (OVD-10) holds both
  grids and swaps them. It also marks the screen unknown after a failed
  write.
- The cursor is not carried across frames. It is unknown at the start
  of every output, so each output's first glyph follows a CUP. That costs
  at most one CUP a frame, which is within SC-003. It is unknown again
  after a cell whose text ends in ZWJ (R38). After a glyph in the last
  column, the tracked column is `cols`, which matches no cell, so the
  next glyph always gets a CUP (R33).
- The terminal's SGR state is default at the start of every output.
  Every non-empty output ends with `CSI 0 m`, and a full redraw starts
  with it. A failed write makes the screen unknown, so the next output
  starts with `CSI 0 m` anyway.
- Cells are compared field by field: length, the text up to length,
  width, and each style field. They are compared as stored, not after
  downsampling. A change the depth cannot show still rewrites the cell,
  which costs bytes but is never wrong. Only leads are compared: a
  continuation equals the screen's whenever its lead does. Width is
  compared, so splitting a wide glyph on screen rewrites both halves.
- Each run of changed cells pays one CUP. Unchanged cells in a gap are
  never re-sent instead.
- A full redraw of a blank frame still writes its prefix and reset. An
  unknown screen must be cleared even when nothing is drawn.
- The R30 bound is computed by `ovd_diff_bound` and uses the size's own
  digit counts for the CUP length. An overflow is `OVD_ERROR_ALLOCATION`,
  as in `ovd_buffer_request`. If an append does not fit (a bug), the
  output is emptied and the result is `OVD_ERROR_IO`.
- An out-of-range depth is `OVD_ERROR_INVALID_ARGUMENT` (R22). It is not
  passed through, as `ovd_color_downsample` does.
- The diff sends cell text as stored. Keeping control bytes out of the
  grid is the canvas's job (R17, FR-032).

### Terminal and frames (OVD-10)

**R44 — Terminal choices.** R21–R25, R31, and R35 leave these open.
Each takes the narrowest reading (issue #11, labeled `assumption`).
- **The system backend waits for OVD-11.** `ovd_terminal_create`
  validates a system config fully, including `color_depth`, then returns
  `OVD_ERROR_UNSUPPORTED` (Art. V, headless first). OVD-11 owns only
  `src/terminal_system.c`, and calls into it from `terminal.c` would not
  link before it exists. So OVD-11 must change the system branch in
  `terminal.c` and add its fields to `struct OvdTerminal` in
  `src/internal.h` (noted on #12).
  *Rejected:* a stub in #12's file; a backend function-pointer table
  (Art. VII).
- **The default backend is `OVD_BACKEND_SYSTEM`.** That is also what a
  zero-initialized config gives. terminal.h and R35 state every other
  default.
- **Errors are checked in a fixed order:** argument errors
  (`OVD_ERROR_INVALID_ARGUMENT`) first, then the backend
  (`OVD_ERROR_UNSUPPORTED`), then state (`OVD_ERROR_STATE`). A call with
  several faults reports the first.
- **One batch at create.** Create allocates the terminal, two grids, and
  the output (sized by `ovd_diff_bound`) in one `ovd_alloc_all` batch,
  so a refusal at any step leaves nothing behind. A resize allocates the
  two grids and the output in one batch, then each owner adopts its
  block (R31).
- **A same-size pending size is a no-op.** A pending size equal to the
  current one is cleared by the next begin with no allocation and no
  full redraw. Only a change in size counts as a resize.
- **A nullptr sink discards output.** The frame counts as delivered.
- **The grids swap only on delivery.** After a delivered frame, the frame
  grid becomes the screen. After `OVD_ERROR_IO` the screen is unknown, so
  which grid holds it no longer matters.
