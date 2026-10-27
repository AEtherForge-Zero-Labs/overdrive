## Why

Issue #4 (OVD-03) supplies the Unicode primitive the canvas (OVD-07)
needs to draw text: decoding caller UTF-8, segmenting it into graphemes,
measuring their width, and replacing everything that could reach the
terminal as a control sequence (FR-031, FR-032; research R15–R17).

## What Changes

- Vendor `EastAsianWidth.txt` and `DerivedGeneralCategory.txt` of the
  pinned Unicode version, with the Unicode license, in `third_party/ucd/`.
- Add `cmake/gen_unicode_tables.cmake`, run by the build, generating the
  wide and extender range tables into the build tree (Art. II); it
  records the Unicode version pin and rejects files of any other version.
- Add the private `src/unicode.h`/`src/unicode.c`: a next-grapheme
  iterator returning the cell's bytes, width, and a replacement flag.
- Add `tests/test_unicode.c` covering every T014 case.
- Record the version pin (assumption, research §D.4) in `research.md`.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements stay in Spec Kit; this transaction
implements T012–T015 without changing them.

### Modified Capabilities

None.

## Impact

Files owned by #4 only, plus append-only lines in `CMakeLists.txt` and
`tests/CMakeLists.txt`, the T012–T015 ticks in `tasks.md`, and the
version-pin entry in `research.md`. No public header changes.
