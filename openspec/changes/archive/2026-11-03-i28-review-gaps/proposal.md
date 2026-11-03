## Why

The post-merge independent review of OVD-01 (#27) passed the code but
found gaps in the records, two missing tests, and two CI nits (issue
#28). Some records disagree with the code. Constitution Art. III and
`AGENTS.md` ask for `constexpr`, which the baseline's Clang 18 rejects
for objects. The stated export rule is looser than the check that runs.

## What Changes

- Records:
  - `specs/001-render-core/research.md` records that Clang 18 lacks C23
    object `constexpr`, so the Clang minimum becomes 19 (new R41). It
    also records that the export check rejects every non-static
    non-`ovd_` symbol in the static archive (R12).
  - `.specify/memory/baseline.md` raises the Clang minimum to 19.
  - `AGENTS.md` "C rules" states the export rule; its `constexpr` line
    stays, now true on every supported compiler.
  - `openspec/changes/i2-build-core/` is archived, and T001–T009 in
    `specs/001-render-core/tasks.md` are ticked.
- Tests:
  - `ovd_result_name` loses its `default:`, so `-Wswitch` under
    `-Werror` breaks the build when an `OvdResult` enumerator is
    unhandled.
  - `tests/CMakeLists.txt` adds a static-archive negative fixture for
    the export check.
- CI:
  - The Clang job moves from `clang-18` to `clang-19`, the new minimum.
  - `baseline.md` notes that CI uses the runner image's CMake and a
    floating `macos-latest`.
  - The native job prints the compiler CMake actually configured, not
    `$CC`.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit.

### Modified Capabilities

None in OpenSpec.

## Impact

No public API change. `src/core.c` keeps its behavior; the test for
out-of-range values still passes. The owner item, requiring `ci-ok` in
the ruleset, is an Art. XVI settings change and goes to its own
`needs-human` issue.
