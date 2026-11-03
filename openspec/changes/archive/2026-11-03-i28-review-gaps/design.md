## Context

The facts behind the records were checked this session:
- Clang's C status page lists "constexpr for object definitions"
  (N3018) as available from Clang 19.
- Ubuntu 24.04 (noble) ships `clang-19` 1:19.1.1-1ubuntu1~24.04.2 in
  universe.
- Constitution Art. III asks for `constexpr` in named constants.
- The last `library-ci` run on `main` used CMake 3.31.6 on the
  `ubuntu-24.04` image and CMake 4.4.4 with Apple clang 21.0.0 on
  `macos-latest`, which resolved to `macos-26-arm64`.

## Goals / Non-Goals

**Goals:** make every record match the code and CI; make a new
`OvdResult` enumerator impossible to miss in `ovd_result_name`; cover the
static-archive branch of the export check.

**Non-Goals:** raising any minimum other than Clang's; pinning CI tool
versions; changing the export check itself; the ruleset (owner).

## Decisions

**D1 — The Clang minimum becomes 19.** This is the issue's first
option. Constitution Art. III asks for `constexpr` in named constants,
and the baseline is subordinate to it, so the baseline cannot keep a
compiler that rejects object `constexpr`. Open PRs already work around
Clang 18 with macros and enums. Ubuntu 24.04 ships `clang-19` 19.1.1 in
universe (packages.ubuntu.com/noble/clang-19), so CI's Clang job moves
to it and `AGENTS.md` keeps its `constexpr` line. The configure check
in `CMakeLists.txt` moves to 19 with the baseline. *Rejected:* the
issue's second option, keeping Clang 18 and narrowing `AGENTS.md`. It
was drafted that way first, and the vocabulary scan before close showed
it contradicts Art. III.

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
`clang-19`, so `$CC` could name another binary.

**D6 — The owner item is split out.** Labeling #28 `needs-human` would
block this PR's merge gate for an item no agent can do. A separate issue
carries it.

## Risks / Trade-offs

- [Open PRs #30 and #31 cite Clang 18 for macro or enum constants] →
  their code still builds on Clang 19; they can switch to `constexpr`
  when they rebase.
- [No code uses object `constexpr` yet, so CI's `clang-19` job does not
  exercise it] → the C status page is the evidence until the first use.

- [`research.md` is also edited by open PRs] → R12 is edited in place,
  far from their hunks; R41 is appended after R40, the current end.
- [The CI step runs only on GitHub] → it is checked by this PR's own CI
  run.
