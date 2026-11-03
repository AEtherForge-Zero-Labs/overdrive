## Context

R15–R17 fix the grapheme, width, and replacement rules; R5 the table
source. The canvas is the only consumer and does not exist yet.

## Decisions

- **Unicode 18.0.0.** The latest released version on the implementation
  date (the UCD `latest` ReadMe names 18.0.0 as final). Pinned once in the
  generator as `OVD_UNICODE_VERSION`; the generator fails unless both
  vendored files name that version on their first line.
- **Tables.** Two sorted, merged, inclusive range tables, emitted as
  `static const` arrays in one generated header in the build tree and
  included only by `src/unicode.c`: wide (East_Asian_Width W or F,
  including any `@missing` default of W or F) and extender
  (General_Category Mn, Me, Cf). Lookup is a binary search.
- **Extenders reduce to Mn, Me, Cf.** In the pinned UCD, U+200D ZWJ is
  Cf and the variation selectors are Mn, and every R17 bidi control is
  Cf; the bidi controls are classified as replaced before the extender
  test, so R15's list is exactly Mn ∪ Me ∪ (Cf − bidi).
- **Iterator.** `ovd_unicode_next_grapheme(text, length, &offset, &out)`
  is transformational (Art. IV): no state beyond the caller's offset.
  `OvdGrapheme` holds the cell text (at most 15 bytes) because a replaced
  base's text (U+FFFD plus extenders) is not a slice of the input.
- **Overflow.** The segment's extent is found first, then its cell text
  is measured; over 15 bytes the whole segment becomes one U+FFFD.
- **Tests compile `src/unicode.c` into the test.** The function has hidden
  visibility, so the shared preset cannot link it from the library.

## Boundaries

- `src/unicode.h`: internal header contract; runtime invariants named in
  its doc comment (cell text is well-formed UTF-8 with no control,
  bidi control, or line/paragraph separator; length 1–15; width 1 or 2).
- Unicode data: external-spec-derived from the pinned UCD.
