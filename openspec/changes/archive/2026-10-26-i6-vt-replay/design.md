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

All recorded as R38 in `research.md`:

- **Widths are injected.** The caller passes a classifier
  (code point → extender, narrow, wide). The interpreter hard-codes only
  R15's ZWJ rule. A rendering test passes an independent, hand-written
  classifier covering only the code points it uses, never `src/unicode`:
  the oracle must not share the module it checks. Alternative: private
  width tables, which would duplicate OVD-03's generated tables.
- **Pending wrap is fatal** whatever DECAWM says, and so is a wide glyph
  that starts in the last column. No C0 control is accepted, so nothing
  can scroll.
- **The cursor starts unknown**, and is unknown again after either
  `?1049` sequence; printing before a CUP fails.
- **Every escape sequence ends a grapheme**, and so does the end of a
  write. A grapheme ending in ZWJ (legal under R15) passes; the next
  glyph after it must follow a CUP (strict side, ASSUMED to break a
  terminal's join). *Superseded:* the first version rejected such a cell
  before a sequence or at the end of a write, which no frame holding
  R15's legal cell could pass (review of 8be01e1).
- **SGR semantics** per ECMA-48 §8.3.117 and xterm (ASSUMED, as
  recalled); bold and faint are kept as independent bits.
- **Exact encodings:** parameters are decimals with no leading zeros,
  parsed with `ckd_mul`/`ckd_add` into `uint32_t` and no digit limit (the
  first version's five-digit cap rejected in-range CUPs); CUP takes two
  explicit in-range parameters; reset is `CSI 0 m`; ED takes only 2 and requires the
  default SGR state (the erase color is terminal-dependent); a private
  mode takes one parameter; no colon sub-parameters, no attribute-off
  codes.
- **A split wide glyph is indeterminate**, never equal to anything,
  until overwritten.
- **Each feed is one write**: a sequence, code point, or grapheme may
  not span two feeds. Errors are sticky.
- **Mode 2026 must balance**: `h` while open or `l` while closed fails.

## Risks / Trade-offs

- [The oracle is stricter than terminals] → intentional; R38 lists every
  rule so OVD-08 can emit to it.
- [A hand-written classifier could mis-model widths] → it covers only
  the code points its test uses, each checked by hand against R16;
  `src/unicode`'s own tables are checked by OVD-03's tests.
