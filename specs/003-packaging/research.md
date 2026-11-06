# Research: Packaging

Decision log for `specs/003-packaging`. Decisions are numbered R201
onward, so they never collide with 001's R1–R48 or 002's R101 onward.
Each carries a provenance tag (CLAUDE.md rule 4):

- **VERIFIED**: read in the cited page while writing this file, or read
  in this repository;
- **INFERRED**: follows from a VERIFIED source but is not stated in it;
- **ASSUMED**: from memory or general practice. The implementing issue
  checks it against the source before code depends on it. An ASSUMED
  item that turns out false is a halt condition for that issue
  (Art. XII), not a silent redesign.

Sources, all CMake 3.28 documentation, the baseline minimum:
- [TOP] `PROJECT_IS_TOP_LEVEL`,
  https://cmake.org/cmake/help/v3.28/variable/PROJECT_IS_TOP_LEVEL.html
- [HELPERS] `CMakePackageConfigHelpers`,
  https://cmake.org/cmake/help/v3.28/module/CMakePackageConfigHelpers.html
- [SOURCES] `target_sources`,
  https://cmake.org/cmake/help/v3.28/command/target_sources.html
- [INSTALL] `install`,
  https://cmake.org/cmake/help/v3.28/command/install.html
- [GENEX] generator expressions,
  https://cmake.org/cmake/help/v3.28/manual/cmake-generator-expressions.7.html

## A. Clarifications carried in from the spec

The spec has no clarification markers. Its Assumptions section fixes
these points: CMake 3.28 is the minimum; fetching at configure time
counts as vendoring; there is no pkg-config; `LICENSE` is installed as
it is; the Windows shared route is checked under Wine; MSVC stays
unsupported.

## B. Decisions

### R201 — Defaults follow whether Overdrive is the top-level project

- **Decision**: `OVERDRIVE_BUILD_TESTS` defaults to
  `PROJECT_IS_TOP_LEVEL`. `OVERDRIVE_BUILD_EXAMPLES` and
  `OVERDRIVE_BUILD_BENCHMARKS` already default to OFF, and stay OFF.
- **Rationale**: [TOP] defines `PROJECT_IS_TOP_LEVEL` as whether "the
  most recently called project() command in the current scope or above"
  was in the top-level `CMakeLists.txt`. It is new in 3.21, below the
  3.28 minimum. VERIFIED. As a subproject, the tests are then off
  unless the consumer sets the option by name (FR-002). The presets set
  every option explicitly, so Overdrive's own builds do not change.
  VERIFIED in `CMakePresets.json`.
- **Alternatives**: a separate `OVERDRIVE_SUBPROJECT` switch that the
  consumer sets. Rejected: it makes every consumer remember it.

### R202 — No global settings change in a consumer's build

- **Decision**: `CMAKE_RUNTIME_OUTPUT_DIRECTORY` is set only when
  Overdrive is the top-level project. The in-source check compares
  `PROJECT_SOURCE_DIR` with `PROJECT_BINARY_DIR`, not the `CMAKE_*`
  pair. Nothing else changes: warnings are already private through
  `ovd_target_warnings`, and `enable_testing()` runs only when tests are
  on.
- **Rationale**: FR-003. The output directory is the one setting in the
  top-level `CMakeLists.txt` that describes the whole build tree rather
  than Overdrive's targets. VERIFIED by reading the file. A variable set
  in a subdirectory does not reach its parent (ASSUMED, CMake's
  directory scope rules), so today it would only move Overdrive's own
  outputs. That is still a decision made on the consumer's behalf.
- **Alternatives**: leave it. Rejected: a shared Overdrive's DLL would
  land in a directory the consumer did not choose.
- **What a test can see**: a subdirectory's normal variables and
  directory properties never reach the parent. The review of this plan
  observed it, on CMake 4.2.3: vendoring today's tree left the parent's
  `COMPILE_OPTIONS`, `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, and
  `CMAKE_C_STANDARD` unchanged. So the output directory is set
  top-level-only for the sake of Overdrive's own outputs, and no test
  checks it in the parent, since such a check could not fail. FR-003
  is tested through the two channels that do reach a consumer:
  - **Usage requirements.** `overdrive`'s `INTERFACE_COMPILE_OPTIONS`
    is unset, and its `INTERFACE_COMPILE_DEFINITIONS` is at most
    `OVERDRIVE_SHARED`. This is checked on the vendored target and on
    the imported one.
  - **Cache entries.** Only `OVERDRIVE_*`, `overdrive_*`, and
    `BUILD_SHARED_LIBS` appear after Overdrive is added. `option()` and
    `try_compile` create those.

  The consumer's warning flags match Overdrive's, so a check of compile
  lines could not see a warning leak. The usage-requirement check can.

### R203 — A second copy stops configuration

- **Decision**: before creating its target, Overdrive checks a global
  property that records the source directory of the first copy. If it
  is set, configuration stops with a message naming both directories.
  Otherwise Overdrive sets it.
- **A consumer target named `overdrive`**: unsupported. If the property
  is unset but `TARGET overdrive` exists, the target is the consumer's
  own. Configuration stops with "a target named overdrive already
  exists (defined in <dir>); Overdrive needs that name", where the
  directory is the target's `SOURCE_DIR`.
- **Rationale**: FR-004. Without the check, the second copy fails on a
  duplicate target name with a message that does not say why. A global
  property is visible from every directory of the build (ASSUMED,
  `set_property(GLOBAL)`). Global properties also start empty on every
  configure, so a re-run does not trip the guard. The cached
  `overdrive_SOURCE_DIR` that `project()` writes would trip it (the
  review of this plan, INFERRED).
- **Alternatives**:
  - **Return early and reuse the first copy.** Rejected: the two copies
    may differ, and a silent reuse hides which one is built.
  - **Rename the internal target**, with `OUTPUT_NAME` and
    `EXPORT_NAME` set to `overdrive`, so a consumer's own `overdrive`
    target can coexist. Rejected for now: no consumer has the
    collision, and the rename touches every test registration
    (Art. VII). The guard makes the limit explicit.

### R204 — Public headers become a header file set

- **Decision**: the five public headers move into a `PUBLIC` file set of
  type `HEADERS`, with base directory `include`. This replaces the
  `BUILD_INTERFACE` include directory and the private `target_sources`
  of the headers. The `overdrive_public_headers` list stays as it is,
  for the header checks in `tests/`.
- **Rationale**: [SOURCES] says the base directories of a `PUBLIC`
  header set are wrapped in `$<BUILD_INTERFACE>` and added to both
  `INCLUDE_DIRECTORIES` and `INTERFACE_INCLUDE_DIRECTORIES`. File sets
  are new in 3.23. [INSTALL] says the files of a `PUBLIC` set are
  installed "under the destination", which defaults to
  `${CMAKE_INSTALL_INCLUDEDIR}`, preserving the structure "relative to
  the file set's base directories". VERIFIED. One list then drives both
  the build and the install, so a header cannot be built but left out of
  the install (FR-010, FR-011).
- **Gap**: [INSTALL] does not say the exported target gains the install
  include directory automatically. Its documented route is `INCLUDES
  DESTINATION`, which "specifies a list of directories which will be
  added to the INTERFACE_INCLUDE_DIRECTORIES target property" of the
  export. VERIFIED. So the install rule names it explicitly. The review
  of this plan observed, on CMake 4.2.3, that the export already
  carries the header set's base directory as `${_IMPORT_PREFIX}/include`
  without `INCLUDES DESTINATION`. So on that version the clause is
  redundant and harmless. Whether 3.28 behaves the same is
  undocumented, and the moved-prefix test is the real proof.
- **Alternatives**: `install(DIRECTORY include/ ...)`. Rejected: it
  would install whatever is in the directory, not the declared list.

### R205 — Package files

- **Decision**:
  - `install(TARGETS overdrive EXPORT overdrive-targets ...)` with
    runtime, library, and archive destinations from `GNUInstallDirs`,
    the header set, and `INCLUDES DESTINATION`;
  - `install(EXPORT overdrive-targets NAMESPACE overdrive:: ...)` to
    `${CMAKE_INSTALL_LIBDIR}/cmake/overdrive`;
  - `overdrive-config.cmake`, generated by
    `configure_package_config_file` from
    `cmake/overdrive-config.cmake.in`. It holds `@PACKAGE_INIT@`, the
    include of the targets file, and `check_required_components`;
  - `overdrive-config-version.cmake` from
    `write_basic_package_version_file` with `SameMinorVersion`.
- **Rationale**: [HELPERS]: `SameMinorVersion` "behaves like
  SameMajorVersion but also requires the minor version to match". So
  0.2 does not satisfy a request for 0.1. VERIFIED. That is FR-007 for
  a 0.x version, and 0.x is the only version that exists. The exported
  name `overdrive` with namespace `overdrive::` gives the same
  `overdrive::overdrive` as the existing build-tree alias (FR-011).
  VERIFIED that the alias exists in `CMakeLists.txt`. `find_package`
  accepts a lowercase `<name>-config.cmake` (ASSUMED, CMake's
  `find_package` documentation).
- **Alternatives**: a hand-written config file. Rejected: the helpers
  handle relocation (R206) and version checks.

### R206 — Relocatable install

- **Decision**: the config file is generated with
  `configure_package_config_file`, and it names no absolute path. The
  targets file comes from `install(EXPORT)`. The installed-route test
  installs to a prefix whose path has a space, moves the prefix, and
  builds the consumer against the moved copy. It also searches the
  installed package files for Overdrive's source and build directories.
- **Rationale**: FR-008, SC-004. [HELPERS]: the helper "avoids
  hardcoded absolute install paths in the generated Config.cmake", and
  computes `PACKAGE_<var>` paths "relative to the package's installed
  location". VERIFIED. That `install(EXPORT)` writes prefix-relative
  paths in the targets file is ASSUMED. The move test checks it.

### R207 — License and notices are installed as files

- **Decision**: `LICENSE` is installed unchanged to
  `${CMAKE_INSTALL_DATAROOTDIR}/doc/overdrive`.
  `third_party/ucd/LICENSE` is installed beside it as `LICENSE-UCD`.
  Nothing else is said about licensing.
- **Rationale**: FR-009. The library compiles in tables generated from
  the vendored UCD (001 R5), so the UCD's notice travels with it.
  VERIFIED that `third_party/ucd/LICENSE` exists and that `LICENSE`
  grants nothing.
- **Not `CMAKE_INSTALL_DOCDIR`**: the review of this plan read
  `GNUInstallDirs` and found that it sets `CMAKE_INSTALL_DOCDIR` as a
  normal variable, `DATAROOTDIR/doc/${PROJECT_NAME}`, which
  subdirectories inherit. It observed a vendored library printing the
  consumer's `share/doc/app` (on 4.2.3). So `CMAKE_INSTALL_DOCDIR`
  could write Overdrive's `LICENSE` over the consumer's. A fixed
  `doc/overdrive` under `DATAROOTDIR` cannot.

### R208 — The routes are proved by CTest, not by a workflow change

- **Decision**: three CTest tests, `consumer_vendored`,
  `consumer_duplicate`, and `consumer_installed`, are registered when `OVERDRIVE_BUILD_TESTS` is
  on. Each runs `tests/consumer/run_consumer.cmake`, which configures,
  builds, and runs the minimal consumer as a separate CMake project.
  The script passes the outer build's settings to that project: the
  generator and make program, the C compiler, the build type,
  `BUILD_SHARED_LIBS`, `CMAKE_C_FLAGS` and the linker flags,
  `CMAKE_SYSTEM_NAME` and `CMAKE_SYSTEM_PROCESSOR`,
  `CMAKE_FIND_ROOT_PATH_MODE_PROGRAM`, and the cross emulator. Each test
  has a `TIMEOUT` of 300 s, since the vendored route compiles the
  library again.
- **Rationale**: FR-014 asks for CI on Linux, macOS, and the Windows
  cross build. Every CI job already runs `ctest` for its preset:
  - Linux: `debug`, `release`, `shared`, `sanitize`, `debug-clang`;
  - macOS: `debug`, `shared`, `sanitize`;
  - Wine: `windows-cross-debug` and `windows-cross-shared`.

  (VERIFIED in `docs/compatibility.md`.) So three tests cover every
  platform, static and shared, with no change to `.github/`, which
  CODEOWNERS reserves for the owner. A test's own `TIMEOUT` takes
  precedence over the preset's 60 s default. This is ASSUMED: the review
  of this plan read `ctest --help` on 4.2.3, which describes `--timeout`
  as "Set the default test timeout".
- **Details from the review of this plan**:
  - The Wine presets use cache variables, not a toolchain file
    (VERIFIED, `CMakePresets.json`), so the forwarded list is complete
    today. The script also forwards `CMAKE_TOOLCHAIN_FILE` when set,
    and fails when `CMAKE_FIND_ROOT_PATH` is set, since forwarding it is
    not designed.
  - The test's command is `${CMAKE_COMMAND} -P <script>`, a path and
    not a target, so CTest does not prepend the emulator. The script
    runs the program through the emulator itself.
  - Each mode works in its own directory, `<build>/consumer/<mode>`, so
    `ctest -j` cannot collide.
- **Alternatives**: new CI jobs in `ci.yml`. Rejected: owner-reserved,
  and they would duplicate the matrix the presets already give.

### R209 — The minimal consumer

- **Decision**: `tests/consumer/` holds a CMake project and one C file.
  - The project requires CMake 3.28, sets C23 without extensions, and
    sets `-Wall -Wextra -Wpedantic -Werror` directory-wide *before*
    adding Overdrive, as the first consumer does.
  - A cache variable picks the route. For `vendored`, the project adds
    Overdrive's source tree as a subdirectory with its own binary
    directory. For `installed`, it finds the package from the prefix it
    is given.
  - It links `overdrive::overdrive` and nothing else.
  - The program includes `<overdrive/overdrive.h>`, creates a headless
    terminal, draws one frame, checks that the output callback ran,
    destroys the terminal, and exits 0.
  - On Windows only, a post-build step copies
    `$<TARGET_RUNTIME_DLLS:consumer>` beside the program.
- **Rationale**: FR-013, SC-001, SC-005. The headless backend needs no
  terminal, so the test runs in CI and under Wine (Art. V). [GENEX]:
  `TARGET_RUNTIME_DLLS` (new in 3.21) lists a target's runtime DLLs
  from its transitive shared dependencies, and is empty "on non-DLL
  platforms". VERIFIED. The copy is guarded by `WIN32` anyway, because
  whether a copy with an empty file list fails is ASSUMED. The same
  step is what README recommends to consumers (FR-015).

### R210 — Static link dependencies

- **Decision**: there is no new rule. Today `overdrive` links nothing
  beyond the C runtime. VERIFIED: `CMakeLists.txt` has no
  `target_link_libraries(overdrive ...)`. The export carries whatever
  `INTERFACE_LINK_LIBRARIES` the target has. The installed static test
  shows that the consumer links with no extra line.

### R211 — What the install holds

- **Decision**: the installed-route test lists the prefix. It fails on
  any required file that is missing, on a license file whose SHA-256
  differs from its source, and on any file outside this set:
  - `include/overdrive/*.h`;
  - the library and, on Windows, its import library and DLL;
  - `<libdir>/cmake/overdrive/*`;
  - `<datarootdir>/doc/overdrive/LICENSE` and `LICENSE-UCD`.

  The consumer finds the package with `CMAKE_FIND_USE_PACKAGE_REGISTRY`
  off, and the test checks that the resulting `overdrive_DIR` is under
  the moved prefix (US2 scenario 1).
- **Rationale**: FR-010, SC-004. The list is the contract (see
  `contracts/consumer.md`). A file outside it is a leak, for example a
  test binary or a private header.

### R212 — Install rules are opt-in when vendored

- **Decision**: `option(OVERDRIVE_INSTALL "Generate Overdrive's install
  rules" ${PROJECT_IS_TOP_LEVEL})`. `include(GNUInstallDirs)`, the
  package helpers, and every `install()` call sit inside
  `if(OVERDRIVE_INSTALL)`. ASSUMPTION, recorded with the `assumption`
  label on the PR (Art. IX).
- **Rationale**: FR-016. A vendoring consumer's `cmake --install` should
  install its own program, not Overdrive's headers, archive, package
  files, or license. The first consumer links Overdrive statically, so
  nothing of Overdrive is needed at run time. A consumer with a shared
  vendored build that needs the library installed sets the option by
  name. Keeping `GNUInstallDirs` inside the block also keeps its
  `CMAKE_INSTALL_*` cache entries out of a vendoring consumer's cache
  (FR-003).
- **Alternatives**: install rules always, which is the common
  open-source default. Rejected: they put Overdrive's files into every
  vendoring consumer's install, unasked.

## C. Tensions

- **FR-014 (CI) vs. owner-reserved `.github/`**: resolved by R208. CI
  runs the routes through the existing test step, with no workflow
  change.
- **Art. VII (no speculative pieces) vs. the installed route, which the
  first consumer does not use**: the owner asked for both routes in this
  feature's request (spec, Input). That request is the present
  requirement; the plan does not justify the route by later
  applications. It adds one template file and install rules, no new
  module. If the owner drops it, User Story 2 and FR-005 to FR-010 go
  with it, and nothing else depends on them.
