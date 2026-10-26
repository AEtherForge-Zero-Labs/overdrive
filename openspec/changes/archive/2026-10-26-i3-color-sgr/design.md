## Context

The renderer compares two styles per changed cell and must write the
smallest correct SGR for the terminal's depth into an output buffer whose
capacity is a proven bound (R30). R7 fixes the SGR subset; R8 fixes when
to use a delta; R9 fixes the downsampling targets.

## Goals / Non-Goals

**Goals:** a pure, allocation-free encoder with an exact worst-case length;
downsampling that matches R9 and is deterministic on ties.

**Non-Goals:** cursor movement, frame framing (R34), color detection
(R10, OVD-06), perceptual color spaces (rejected in R9).

## Decisions

- Transformational module (Art. IV): input styles and depth, output bytes;
  nothing is retained.
- Both styles are normalized (unknown kind → default, unknown attribute
  bits dropped, unused color fields ignored) and downsampled before they
  are compared, so a change the depth cannot show writes nothing.
- Delta when the new attribute set contains the old one: added attributes,
  then changed foreground, then changed background (39/49 for a return to
  default). Otherwise one sequence starting with parameter 0, followed by
  every attribute and every non-default color. ECMA-48 SGR applies its
  parameters in order, so `CSI 0;1 m` equals `CSI 0 m CSI 1 m` and is
  shorter. Attribute removal needs reset because 22–29 are outside R7.
- Indexed 0–15 are written as 30–37/90–97 (40–47/100–107) at every color
  depth; indexed 16–255 as `38;5;n` at 256 and truecolor. RGB is written
  as `38;2` only at truecolor.
- RGB → 256 searches indices 16–255 only (0–15 follow the user's theme,
  so their RGB is unknown); ties go to the lowest index. RGB and 16–255 →
  16 use xterm's default 16-color values; ties go to the lowest index.
- An out-of-range depth is treated as NONE (the public API validates
  depth, so this only guards internal misuse).
- `OVD_COLOR_SGR_MAX_LENGTH` (48) is a macro, not a `constexpr` object,
  because the baseline's Clang 18 lacks C23 object `constexpr`.
- Every non-static name is `ovd_color_…`: the export check also runs on
  the static archive and rejects any global symbol without `ovd_`.
- Encode into a local buffer, then copy only when it fits: a refused
  encode leaves the caller's buffer untouched.

## Risks / Trade-offs

- The xterm default palette and SGR semantics are cited from xterm's
  documentation, not observed in a terminal (Art. X); manual checks are
  OVD-12's.
