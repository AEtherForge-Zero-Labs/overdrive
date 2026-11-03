## Why

The post-merge independent review of OVD-01 (#27) passed the code but
found gaps in the records, two missing tests, and two CI nits (issue
#28). Some records disagree with the code: `AGENTS.md` tells agents to
use `constexpr`, which Clang 18 rejects for objects, and the stated
export rule is looser than the check that runs.

## What Changes

- Records:
  - `specs/001-render-core/research.md` records that Clang 18 lacks C23
    object `constexpr` (new R41) and that the export check rejects every
    non-static non-`ovd_` symbol in the static archive (R12).
  - `.specify/memory/baseline.md` amends the provisional Clang minimum.
  - `AGENTS.md` "C rules" narrows the `constexpr` line and states the
    export rule.
  - `openspec/changes/i2-build-core/` is archived, and T001–T009 in
    `specs/001-render-core/tasks.md` are ticked.
- Tests:
  - `ovd_result_name` loses its `default:`, so `-Wswitch` under
    `-Werror` breaks the build when an `OvdResult` enumerator is
    unhandled.
  - `tests/CMakeLists.txt` adds a static-archive negative fixture for
    the export check.
- CI:
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
