## 1. Records

- [x] 1.1 research.md: R41 records Clang 18's missing object `constexpr` and the measured CI tool versions; R12 states that every non-static symbol carries `ovd_`
- [x] 1.2 baseline.md: amend the provisional Clang minimum; note runner-provided CMake and floating `macos-latest`
- [x] 1.3 AGENTS.md "C rules": narrow the `constexpr` line; state the export rule
- [x] 1.4 Tick T001–T009 in specs/001-render-core/tasks.md
- [x] 1.5 Archive `openspec/changes/i2-build-core/`

## 2. Tests

- [x] 2.1 `src/core.c`: remove `default:` from `ovd_result_name`; watch a scratch enumerator fail the build, then pass without it
- [x] 2.2 `tests/CMakeLists.txt`: static-archive negative fixture rejected with "Non-ovd export"; watch it fail against a check that skips static archives

## 3. CI

- [x] 3.1 ci.yml: the native job prints the configured compiler's version after configure
- [x] 3.2 Open the owner issue for requiring `ci-ok` in the ruleset

## 4. Checks

- [x] 4.1 debug, debug-clang, shared, release, and windows-cross-debug build with no warnings and pass ctest
- [x] 4.2 `openspec validate --all --strict` and the docs form check pass
