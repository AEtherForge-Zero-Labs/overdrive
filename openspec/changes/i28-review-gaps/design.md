## Context

The facts behind the records were checked this session:
- Clang's C status page lists "constexpr for object definitions"
  (N3018) as available from Clang 19.
- The last `library-ci` run on `main` used CMake 3.31.6 on the
  `ubuntu-24.04` image and CMake 4.4.4 with Apple clang 21.0.0 on
  `macos-latest`, which resolved to `macos-26-arm64`.

## Goals / Non-Goals

**Goals:** make every record match the code and CI; make a new
`OvdResult` enumerator impossible to miss in `ovd_result_name`; cover the
static-archive branch of the export check.

**Non-Goals:** raising a tool minimum; pinning CI tool versions;
changing the export check itself; the ruleset (owner).

## Decisions

**D1 — Clang 18 stays the minimum; object `constexpr` is not used.**
This is the issue's second option. Raising the minimum needs a new
compiler package in CI and a baseline change for one feature that the
headers do not need. *Rejected:* raising to Clang 19.

**D2 — The stricter check is the rule.** R12 and `AGENTS.md` say that
every non-static symbol in the library carries `ovd_`, hidden or not,
because the static-archive listing cannot tell hidden from exported.
*Rejected:* relaxing the check to the shared library only, which would
let a generic hidden name collide in a consumer's static link.

**D3 — Enum coverage by the compiler.** Remove `default:` and return the
unknown name after the switch. `-Wswitch` (in `-Wall` for GCC, on by
default in Clang) then reports a missing enumerator, and `-Werror`
fails the build. Out-of-range values still reach the trailing return,
which `test_core` already checks. *Rejected:* a test-side count
enumerator, which would add a public symbol (Art. VII).

**D4 — CI facts as conditions.** `baseline.md` is project-lifetime, so
it states the condition (runner-provided CMake, a floating
`macos-latest`, the minimums not exercised by CI). The measured versions
go in R41. *Rejected:* pinning, which needs tool installation steps for
no present failure.

**D5 — Print the configured compiler.** After configure, the native job
reads `CMAKE_C_COMPILER` from the build tree's cache and runs it with
`--version`. `debug-clang` sets `CMAKE_C_COMPILER=clang` while `CC` is
`clang-18`, so `$CC` could name another binary.

**D6 — The owner item is split out.** Labeling #28 `needs-human` would
block this PR's merge gate for an item no agent can do. A separate issue
carries it.

## Risks / Trade-offs

- [`research.md` is also edited by open PRs] → R12 is edited in place,
  far from their hunks; R41 is appended after R40, the current end.
- [The CI step runs only on GitHub] → it is checked by this PR's own CI
  run.
