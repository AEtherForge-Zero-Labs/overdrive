## Context

R7 fixes the protocol subset, R33 the deferred-wrap rule, R34 the frame
framing. The oracle must be stricter than any real terminal: anything a
terminal would tolerate but that depends on terminal-specific behavior
is an error or an indeterminate cell.

## Goals / Non-Goals

**Goals:** decode R7 output into a readable screen; reject everything
else; never depend on `src/unicode` (sibling issue #4).

**Non-Goals:** checking a frame's R34 order (OVD-08's test, T027);
modeling the primary screen behind mode 1049; a resizable screen.

## Decisions

All recorded as R37 in `research.md`:

- **Widths are injected.** The caller passes a classifier
  (code point → extender, narrow, wide). The interpreter hard-codes only
  R15's ZWJ rule. Alternative: private width tables, which would
  duplicate OVD-03's generated tables and could disagree with them.
- **Pending wrap is fatal** whatever DECAWM says, and so is a wide glyph
  that starts in the last column. No C0 control is accepted, so nothing
  can scroll.
- **The cursor starts unknown**; printing before a CUP fails.
- **Exact encodings:** CUP takes two explicit in-range parameters with no
  leading zeros; reset is `CSI 0 m`; ED takes only 2 and requires the
  default SGR state (the erase color is terminal-dependent); a private
  mode takes one parameter; no colon sub-parameters, no attribute-off
  codes.
- **A split wide glyph is indeterminate**, never equal to anything,
  until overwritten.
- **Each feed is one write**: a sequence, code point, or grapheme may
  not span two feeds. Errors are sticky.
- **Mode 2026 must balance**: `h` while open or `l` while closed fails.

## Risks / Trade-offs

- [The oracle is stricter than terminals] → intentional; R37 lists every
  rule so OVD-08 can emit to it.
- [An injected classifier could mis-model widths] → OVD-08 passes one
  built from `src/unicode`, the R16 policy the canvas uses.
