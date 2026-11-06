# Implementation Plan: Packaging

**Branch**: `003-packaging` | **Date**: 2026-11-06 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/003-packaging/spec.md`

## Summary

Make Overdrive consumable from another CMake project by two routes that
give the same `overdrive::overdrive` target.

- **Vendored subproject.** Defaults follow whether Overdrive is the
  top-level project, so a consumer gets no Overdrive tests and no
  install rules. Overdrive sets its output directory only as the
  top-level project. A second copy is refused, and so is a consumer
  target already named `overdrive`.
- **Installed package.** The public headers become a header file set
  that drives both the build and the install. The install rules, behind
  `OVERDRIVE_INSTALL`, add:
  - an exported target;
  - a relocatable config file;
  - a `SameMinorVersion` version file;
  - `LICENSE` and the UCD notice, under `doc/overdrive`.

Both routes are proved by three CTest tests that build and run a minimal
consumer as a separate project. Every CI job already runs CTest for its
preset, so CI covers Linux, macOS, and Wine, static and shared, with no
workflow change. The decisions are in [research.md](research.md)
(R201–R212), and the build contract is in
[contracts/consumer.md](contracts/consumer.md).

## Technical Context

**Language/Version**: ISO C23 (unchanged); CMake 3.28 (unchanged
minimum)
**Primary Dependencies**: CMake's own `GNUInstallDirs` and
`CMakePackageConfigHelpers` modules. No new library.
**Storage**: N/A
**Testing**: CTest. Three new tests run a nested CMake configure, build,
and run of `tests/consumer/`. The existing header checks stay as they
are.
**Target Platform**: as 001 and 002: Linux, macOS, and Windows through
the MinGW-w64 cross build and Wine
**Project Type**: library
**Performance Goals**: none (Art. XV). Each consumer test has a
300 s timeout, a limit, not a claim.
**Constraints**: no change to the public C API; no change to
`.github/` (owner-reserved); no change to Overdrive's own builds
through the presets
**Scale/Scope**: about 60 lines of CMake in `CMakeLists.txt`, one
config template, a consumer project of about 40 lines of CMake and
40 of C, and a test script of about 120 lines

## Constitution Check

*Gate against constitution v1.4.2. Checked before research and again
after the design below.*

| Article | Gate | Status |
|---|---|---|
| I Contracts are types | Every boundary has a contract form | PASS. The build interface is a written contract (`contracts/consumer.md`). It is runtime-validated by the consumer tests: the target, the file list, relocation, and refusing a second copy. No C boundary changes. |
| II Generated code separate | Generated files stay out of hand-written ones | PASS. The config and version files are generated into the build tree from a template. Nothing generated is committed. |
| III Strict typing | — | PASS. No C types change. |
| IV Ownership | Paradigm per module | PASS. No new C module. The consumer program is test-only. |
| V Mechanic before interface | Headless first | PASS. The consumer uses the headless backend. The routes are proved before README documents them. |
| VI Human-readable | Readable state | PASS. The install contract is a table. A failing test names its step and the stray file. |
| VII No speculation | Each piece justified | PASS. Table below. The installed route rests on the owner's request in this feature (research §C). |
| VIII Clean checkout | Declared tools only | PASS. Only CMake modules that ship with CMake. No new preset. |
| IX Agent discipline | Assumptions recorded | PASS. Each ASSUMED item in research names the check that confirms it. |
| X Evidence | Unverifiable areas named | PASS. A native Windows consumer is a manual check (spec Assumptions). |
| XI Check integrity | No check weakened | PASS. The new tests only add checks. The header checks and `check_exports` are unchanged. |
| XIII Security | — | PASS. No new input handling. The test script passes paths as arguments, not through a shell. |
| XIV Identity/API | Every public name listed | PASS. New public names: the package `overdrive`, the options' new defaults, the install layout. No new `ovd_` symbol. |
| XV Claims | No unmeasured claims | PASS. README states only what the consumer tests show. |
| XVI Process | Issues, gated merge | PASS. `tasks.md` becomes issues after owner approval. No `.github/` change. |

**Tension recorded:** FR-014 (CI) vs. owner-reserved `.github/`.
Resolved by R208: CI runs the routes through its existing test step.

## Public build interface (Art. XIV plan gate)

| Name | Kind | Change |
|---|---|---|
| `overdrive::overdrive` | CMake target | now also installed and exported |
| `overdrive` | `find_package` name | new |
| `OVERDRIVE_BUILD_TESTS` | option | default is now `PROJECT_IS_TOP_LEVEL` (ON for Overdrive's own builds, as before) |
| `OVERDRIVE_INSTALL` | option | new; default `PROJECT_IS_TOP_LEVEL`; gates every install rule (R212, an assumption) |
| install layout | files | new (`contracts/consumer.md`) |

## New pieces (Art. VII plan gate)

| Piece | Present requirement |
|---|---|
| `cmake/overdrive-config.cmake.in` | FR-005: `find_package(overdrive)` needs a config file |
| `tests/consumer/CMakeLists.txt`, `tests/consumer/main.c` | FR-013: a consumer that proves both routes |
| `tests/consumer/run_consumer.cmake` | FR-014 and R208: the nested configure, build, and run, plus the install checks |

## Terminal modes (CLAUDE.md, Repo specifics)

Not applicable. `ovd_terminal_create` and `ovd_terminal_destroy` do not
change. The consumer uses the headless backend, which sets no terminal
mode.

## Project Structure

### Documentation (this feature)

```text
specs/003-packaging/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/consumer.md
├── checklists/requirements.md
└── tasks.md            (speckit-tasks)
```

### Source Code (repository root)

```text
CMakeLists.txt                     defaults, output dir, duplicate guard, header set, install rules
cmake/overdrive-config.cmake.in    new: package config template
tests/CMakeLists.txt               registers consumer_vendored, consumer_installed, consumer_duplicate
tests/consumer/CMakeLists.txt      new: minimal consumer project
tests/consumer/main.c              new: headless create, one frame, destroy
tests/consumer/run_consumer.cmake  new: nested configure, build, run, install checks
README.md                          how to consume, both routes
docs/architecture.md               a short Packaging section
```

**Structure Decision**: single library project, layout as in
`AGENTS.md`. `tests/consumer/` is a test-support project and is not
installed.

## Workstreams (for speckit-tasks)

Three issues, run in order. The first two both edit `CMakeLists.txt`
and the test script, so they cannot run in parallel (Art. IX).

1. **OVD-22 Vendored route.** R201–R204, the consumer project and
   script, and the `consumer_vendored` and `consumer_duplicate` tests.
   Covers FR-001–004, FR-011–013, SC-001, SC-003, SC-005. The header
   file set lands here, because the vendored route already uses it.
2. **OVD-23 Installed route.** R205–R207 and R210–R212: install rules
   behind `OVERDRIVE_INSTALL`, the config template, the license files,
   and the `consumer_installed` test with its file-list, checksum,
   relocation, and build-path checks, plus the check that a vendored
   install adds nothing. Covers FR-005–010, FR-014, FR-016, SC-002,
   SC-004. After 1.
3. **OVD-24 Docs.** README's "Using Overdrive" section with both routes
   and the Windows DLL step, `docs/architecture.md`, and
   `docs/compatibility.md`'s description of what CI runs. Covers
   FR-015. After 2.

## Complexity Tracking

| Item | Why needed | Simpler alternative rejected because |
|---|---|---|
| Nested CMake builds inside CTest | FR-014 on every CI platform without editing `.github/` | New workflow jobs: owner-reserved, and they would repeat the preset matrix |
| Duplicate-copy guard | FR-004 | CMake's own duplicate-target error does not say that two copies were added |
| Relocation test, moving the prefix | FR-008 | Searching for absolute paths alone misses a path built some other way |
