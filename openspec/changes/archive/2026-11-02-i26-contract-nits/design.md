## Context

`include/overdrive/` was copied from `specs/001-render-core/contracts/`
by OVD-01 and is byte-identical to it. R26 and R28 are restated in
`terminal.h`'s doc comments, so a fix that reaches only `research.md` or
only the proposed header would leave the public contract disagreeing
with its decision log.

## Goals / Non-Goals

**Goals:** each nit fixed with the narrowest wording that settles it;
both `terminal.h` copies stay identical.

**Non-Goals:** no new decision number; no change to symbols, types, or
signatures; no edit to the §E "Unverified" paragraph or to the end of
`research.md`, where open PRs append their decisions.

## Decisions

**D1 — Destroy before any frame (R26).** The cursor move uses the last
row of the last frame `ovd_frame_end` closed, or, if none has, of the
size read at create (R28 step 5). This is the issue's stated fallback,
and that size is always known by the time any exit sequence is written.
*Rejected:* the current buffers' size, which an abandoned begin can
change without anything new reaching the screen; omitting the move with
no frame, which adds a second exit-sequence variant.

**D2 — Enter-write failure (R28).** The best-effort exit sequence is
R26's, with the alternate screen counted as entered whenever it was
configured, so it includes `CSI ? 1049 l`. A failed write does not show
whether `CSI ? 1049 h` took effect. A needless `CSI ? 1049 l` restores the
cursor as DECRC does (xterm control-sequence reference, Patch #411), and
DECRC with nothing saved moves the cursor home (VT510 reference, DECRC).
A missing one would leave the shell on the alternate screen.
*Rejected:* deciding from the byte count of the partial write, which
needs the platform write to report it (Art. VII).

**D3 — R34 attribution.** The proposal for mode 2026 says nothing about
terminals that lack it. It detects support with DECRQM, which needs
input. The xterm reference says ECMA-48 describes only correct behavior.
So the "ignored when unsupported" claim is tagged ASSUMED, and the
existing "Unverified" paragraph already lists it.

**D4 — Rewrap.** Prose lines over 78 characters in §E and `spec.md` are
rewrapped to the surrounding width. The `spec.md` User Story 1 heading
(81 characters) stays on one line: a Markdown heading cannot wrap.

**D5 — Public header in scope.** The issue names "contracts headers". The
public copy is changed too, comments only, under the contract-change rule
in `docs/process/swarm.md`: recorded in `research.md`, labeled
`assumption`.

## Risks / Trade-offs

- [Open PRs #30–#33 also edit `research.md`] → edits stay inside R19,
  R22, R26, R28, R30, and R34, away from the open PRs' insertion points.
- [DECRC and mode 1049 behavior is cited, not observed] → stated inline
  in R28. The OVD-12 manual checklist covers the real terminals.
