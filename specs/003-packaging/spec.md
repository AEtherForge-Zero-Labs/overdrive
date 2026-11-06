# Feature Specification: Packaging

**Feature Branch**: `003-packaging`

**Created**: 2026-11-06

**Status**: Draft

**Input**: "Feature 003: packaging for liboverdrive. Present requirement:
the first consumer, a separate C23 console task manager (GCC 14, CMake
3.28 presets, -Wall -Wextra -Wpedantic -Werror on all its targets,
Linux/WSL primary, Windows supported), will replace its own terminal
layer with Overdrive and must be able to build and link it from its own
CMake project. Today Overdrive has no install or export rules, its
include directory is BUILD_INTERFACE only, OVERDRIVE_BUILD_TESTS defaults
ON (so a consumer adding it as a subdirectory would build Overdrive's
tests), and nothing tests consumption. The feature: a consumer can (a)
vendor the source tree and add it as a subdirectory, and (b) use an
installed copy through find_package(overdrive), both through the same
target name overdrive::overdrive, static or shared; Overdrive's tests,
examples, benchmarks, and warning flags do not leak into or burden the
consumer; public headers compile warning-free under the consumer's
strict flags; the installed package carries its version and the license
file state; CI proves both routes on Linux, macOS, and the Windows cross
build. Out of scope: widgets (a later feature), package managers (vcpkg,
Conan, pkg-config unless trivially derived), release tagging and
publication (owner-only), choosing a license (owner-only)."

## Scope in one paragraph

Another CMake project can build and link Overdrive in one of two ways.
It can copy Overdrive's source tree into its own and add it as a
subproject. Or it can use a copy installed elsewhere on the machine and
find it by package name. Either way, the project links one target name,
`overdrive::overdrive`, and gets the public headers, the library (static
or shared), and the compile settings the headers need. It gets nothing
it did not ask for: no Overdrive tests, examples, or benchmarks, and no
Overdrive warning flags on its own targets. The first consumer is the
owner's console task manager, which will replace its own terminal layer
with Overdrive. Overdrive's public API does not change.

## User Scenarios & Testing *(mandatory)*

The users are developers of C applications that build with CMake. The
first one is the task manager: C23, GCC 14, CMake 3.28 presets, and
`-Wall -Wextra -Wpedantic -Werror` on every target.

### User Story 1 — Vendor the source tree (Priority: P1)

A developer copies Overdrive's source tree into their project, for
example under `third_party/overdrive`, adds it as a subproject, and
links `overdrive::overdrive`. Their program includes
`<overdrive/overdrive.h>`, builds, and runs.

**Why this priority**: It is the route the first consumer will use. That
project already vendors its one other dependency this way, and its build
must work from a clean checkout without network access or system
packages.

**Independent Test**: A minimal consumer project in Overdrive's own
tree vendors Overdrive this way, builds with the first consumer's flags,
and runs a program that creates a headless terminal, draws a frame, and
destroys it.

**Acceptance Scenarios**:

1. **Given** a consumer project that adds Overdrive's tree as a
   subproject and links `overdrive::overdrive`, **When** it configures
   and builds, **Then** the build succeeds with zero warnings and the
   program runs and exits 0.
2. **Given** that consumer, **When** it configures, **Then** no
   Overdrive test, example, or benchmark target exists in its build,
   unless the consumer turned one on by name.
3. **Given** a consumer that compiles its own targets with `-Wall
   -Wextra -Wpedantic -Werror`, **When** its sources include every public
   header, **Then** they compile with zero warnings.
4. **Given** a consumer that asks for a shared library, **When** it
   builds, **Then** Overdrive is built shared and the program runs
   against it.

---

### User Story 2 — Use an installed copy (Priority: P2)

A developer builds and installs Overdrive once, to a prefix of their
choice. Their project then finds it by package name, links
`overdrive::overdrive`, and builds, without Overdrive's source tree.

**Why this priority**: It is the route for the owner's later
applications, which share one installed copy instead of each vendoring
the tree. The first consumer does not need it to start.

**Independent Test**: CI installs Overdrive to a temporary prefix,
deletes the build tree, and builds the same minimal consumer against the
installed copy alone.

**Acceptance Scenarios**:

1. **Given** Overdrive installed to a prefix, **When** a consumer finds
   the package from that prefix and links `overdrive::overdrive`,
   **Then** it builds with zero warnings and runs, with Overdrive's
   source and build trees absent.
2. **Given** an installed static library, **When** the consumer links
   it, **Then** the libraries Overdrive itself needs are linked
   automatically, with no extra line in the consumer.
3. **Given** an installed shared library, **When** the consumer builds,
   **Then** the program runs. On Windows, this applies when the
   library's DLL is beside the program or on `PATH`.
4. **Given** a consumer that asks for a version, **When** the installed
   version does not satisfy it, **Then** finding the package fails with
   a message naming both versions.
5. **Given** an install, **When** its files are listed, **Then** the
   prefix holds the public headers, the library, the package files,
   Overdrive's `LICENSE` exactly as in the source tree, and the notices
   of the vendored third-party data compiled into the library. It holds
   no test, example, benchmark, private header, or build-tree path.

---

### User Story 3 — Both routes stay working (Priority: P2)

A maintainer changes Overdrive's build. CI tells them if either route
broke, on every platform Overdrive's CI already covers.

**Why this priority**: The routes are a contract with consumers. Without
a check, a build change that only Overdrive's own tests exercise could
break them silently.

**Independent Test**: CI runs both consumer builds, and a deliberately
broken change, such as dropping a public header from the install, makes
the job fail.

**Acceptance Scenarios**:

1. **Given** a pull request, **When** CI runs, **Then** the vendored
   route and the installed route each build and run the minimal consumer
   on Linux and macOS, static and shared.
2. **Given** a pull request, **When** CI runs the Windows cross build,
   **Then** both routes build the consumer with MinGW-w64, and the
   consumer runs under Wine.
3. **Given** a change that breaks a route, **When** CI runs, **Then**
   the job for that route fails and names the step.

### Edge Cases

- **Consumer flags stricter than Overdrive's.** A consumer may set
  warnings directory-wide, which then also apply to a vendored
  Overdrive's sources. Overdrive's sources build clean under the first
  consumer's flags.
- **Consumer with a different compiler.** Overdrive requires GCC 14,
  Clang 19, or Apple Clang 17. A vendored copy refuses any other
  compiler at configure time with its existing message, not with
  compile errors.
- **Consumer already has a target named `overdrive`.** Overdrive's own
  target name is internal. Consumers link only the namespaced name.
- **Overdrive vendored twice**, for example by the consumer and by one
  of its dependencies. Adding a second copy fails at configure time with
  a message, not with confusing duplicate-target errors.
- **An install to a path with spaces.** The package works there.
- **Relocated install.** An installed prefix copied to another path
  still works. No absolute build-machine path is baked into the package
  files.
- **Mixed static and shared.** Finding the package gives the variant
  that was installed. Whether the consumer needs `OVERDRIVE_SHARED`
  defined follows from that variant automatically, never by hand.
- **Overdrive's tests wanted inside a consumer.** A consumer that turns
  `OVERDRIVE_BUILD_TESTS` on by name gets them. That is not a default.

## Requirements *(mandatory)*

### Functional Requirements

**Vendored subproject**

- **FR-001**: Overdrive's source tree MUST be addable as a subproject of
  another CMake project. It MUST provide `overdrive::overdrive` there.
- **FR-002**: As a subproject, Overdrive MUST NOT build its tests,
  examples, or benchmarks by default. Each stays available through its
  existing `OVERDRIVE_BUILD_*` option. As the top-level project, the
  defaults are unchanged.
- **FR-003**: As a subproject, Overdrive MUST NOT change the consumer's
  global build settings: compile options, output directories, the C
  standard of other targets, or whether testing is enabled. Overdrive's
  warning flags MUST stay private to Overdrive's own targets.
- **FR-004**: A second copy of Overdrive added to the same build MUST
  stop configuration with a message naming both source directories.

**Installed package**

- **FR-005**: Overdrive MUST install its public headers, its library,
  and package files, so that a consumer finds it by the package name
  `overdrive` and links `overdrive::overdrive`.
- **FR-006**: The installed target MUST carry what the public headers
  need to compile: the include directory, the C23 requirement, and
  `OVERDRIVE_SHARED` when the library is shared. A static install MUST
  carry the system libraries Overdrive links against.
- **FR-007**: The package MUST carry Overdrive's version, and finding it
  MUST check a requested version. While the major version is 0, a
  request is satisfied only by the same major and minor version, because
  every 0.x interface may change (README).
- **FR-008**: The install MUST be relocatable: no absolute path of the
  machine that built it appears in the installed package files.
- **FR-009**: The install MUST include Overdrive's `LICENSE` unchanged,
  and the license notices of vendored third-party data compiled into the
  library. Installing MUST NOT add or imply any license grant.
- **FR-010**: The install MUST NOT include tests, examples, benchmarks,
  private headers under `src/`, or generated build-tree files other than
  the package files.

**Both routes**

- **FR-011**: Both routes MUST give the same target name, the same
  public headers, and the same compile requirements, so a consumer can
  switch routes by changing only how it gets Overdrive.
- **FR-012**: Every public header MUST compile with zero warnings in a
  consumer translation unit built with C23 and `-Wall -Wextra -Wpedantic
  -Werror`, on GCC 14, Clang 19, and Apple Clang.
- **FR-013**: A minimal consumer project in Overdrive's repository MUST
  exercise both routes. It creates a headless terminal, draws one frame,
  checks the output callback ran, and destroys the terminal.
- **FR-014**: CI MUST build and run that consumer through both routes on
  Linux (GCC 14) and macOS (Apple Clang), static and shared, and build it
  through both routes in the Windows cross build, running it under Wine.
- **FR-015**: `README.md` MUST say how to consume Overdrive by each
  route, in at most a few lines of the consumer's CMake each.

### Key Entities

- **Consumer project**: a CMake project that builds an application
  linking Overdrive. It owns its warning flags and its build settings.
- **Vendored copy**: Overdrive's source tree inside a consumer's tree,
  added as a subproject.
- **Installed package**: the headers, library, package files, license,
  and notices under an install prefix.
- **Minimal consumer**: the test project in Overdrive's repository that
  proves both routes.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A consumer gets Overdrive with at most 2 lines of its own
  CMake per route: one that adds or finds it, and one that links it.
- **SC-002**: Through either route, the minimal consumer builds with 0
  warnings and runs to exit status 0 in every CI job: Linux and macOS,
  static and shared, and both Windows cross builds under Wine.
- **SC-003**: A vendored configure creates 0 Overdrive test, example, or
  benchmark targets by default.
- **SC-004**: An installed prefix contains 0 files from Overdrive's
  `tests/`, `examples/`, `bench/`, or `src/`, and 0 absolute paths of
  the build machine.
- **SC-005**: The minimal consumer reproduces the first consumer's build
  shape: C23 without extensions, CMake 3.28, and `-Wall -Wextra
  -Wpedantic -Werror` set directory-wide before Overdrive is added, so
  that they also reach a vendored Overdrive's sources. It builds with 0
  warnings. Adoption by the task manager itself happens in that
  project, not here.

## Assumptions

- **CMake 3.28** stays the minimum for Overdrive and for consumers,
  matching the baseline and the first consumer.
- **Fetching the source at configure time** (for example with
  `FetchContent`) is the vendored route under another name. It is
  supported to the extent that the vendored route is, with no extra
  work.
- **pkg-config** is out of scope. Both of the owner's consumers use
  CMake, so no present requirement asks for it.
- **The license** stays "all rights reserved" until the owner chooses
  one. This feature installs the file as it is and decides nothing about
  it.
- **The Windows shared route** is checked by cross building and running
  under Wine, as Overdrive's other Windows checks are. A native Windows
  consumer build is a manual check.
- **MSVC** stays unsupported, as the baseline says. Packaging does not
  change which compilers Overdrive supports.

## Out of scope

- Widgets for menus, prompts, lists, and tables (a later feature,
  driven by the same consumer).
- Package managers: vcpkg, Conan, Homebrew formulae, distribution
  packages.
- Release tags, published archives, and repository visibility (owner
  decisions, Art. XVI).
- Choosing a license (owner decision, Art. XVI).
- Changes to the public C API.

## Dependencies on decisions outside this spec

- None. The license, publication, and the reference consumer's adoption
  are owner decisions this feature does not need in order to be done.
