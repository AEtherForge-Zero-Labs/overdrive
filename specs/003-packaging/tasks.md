# Tasks: 003-packaging

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [quickstart.md](quickstart.md),
[contracts/consumer.md](contracts/consumer.md)

Format: `- [ ] T### [P] [Story] Description naming the file (depends on …)`.

- **Numbering.** Task ids start at T201 and decisions at R201, so
  neither collides with 001 or 002.
- **Parallel.** `[P]` marks a task that is parallel-safe with the other
  `[P]` tasks in its phase (disjoint files).
- **Issues.** Each phase maps to one GitHub issue (`OVD-NN`, continuing
  002's numbering; see [workgraph.json](workgraph.json)). Once the
  issues exist, lines gain `→ #<number>`.
- **Tests first.** Every behavior task comes after its test task, which
  must be seen failing first (`AGENTS.md`, Definition of done).
- **Assumptions.** A task that depends on an ASSUMED item in
  `research.md` first checks it against the cited source and quotes it
  in the PR. A false assumption halts the issue (Art. XII).

## Phase 1 — OVD-22 Vendored route (US1, blocks everything)

Goal: a consumer adds Overdrive's tree as a subproject, links
`overdrive::overdrive`, and gets no Overdrive tests (US1).
Independent test: `ctest --preset debug -R '^consumer_(vendored|duplicate)$'`.

- [ ] T201 [US1] Write `tests/consumer/main.c` and
  `tests/consumer/CMakeLists.txt` (R209).
  - The project needs CMake 3.28 and C23 without extensions. It sets
    `add_compile_options(-Wall -Wextra -Wpedantic -Werror)` before
    adding Overdrive (SC-005).
  - The cache variable `OVERDRIVE_CONSUMER_ROUTE` is `vendored` or
    `duplicate`. `vendored` calls `add_subdirectory` on
    `OVERDRIVE_SOURCE_DIR` with a binary directory. `duplicate` does it
    twice, with two binary directories.
  - It gets and links Overdrive in exactly two lines, `add_subdirectory`
    and `target_link_libraries` (SC-001).
  - It links `overdrive::overdrive` only.
  - After adding Overdrive, it stops configuration with
    `message(FATAL_ERROR)` if its own directory's `COMPILE_OPTIONS`,
    `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, or `CMAKE_C_STANDARD` differ from
    their values before the call (FR-003).
  - A `WIN32`-guarded post-build step copies
    `$<TARGET_RUNTIME_DLLS:consumer>` beside the program.
  - The program uses only the public API:
    - `ovd_terminal_config_default`, with an `OvdSink` that counts its
      calls;
    - `ovd_terminal_create` (headless);
    - `ovd_frame_begin`, `ovd_canvas_text`, `ovd_frame_end`;
    - `ovd_terminal_destroy`.

    It returns 0 only if the sink was called.
- [ ] T202 [US1] Write `tests/consumer/run_consumer.cmake` (R208), a
  `cmake -P` script taking `-DMODE=vendored|duplicate` and the outer
  build's settings as `-D` arguments.
  - It configures `tests/consumer` into a fresh directory under the
    outer build tree. It passes on the generator, the make program,
    `CMAKE_C_COMPILER`, `CMAKE_BUILD_TYPE`, `BUILD_SHARED_LIBS`,
    `CMAKE_C_FLAGS`, `CMAKE_EXE_LINKER_FLAGS`,
    `CMAKE_SHARED_LINKER_FLAGS`, `CMAKE_SYSTEM_NAME`,
    `CMAKE_SYSTEM_PROCESSOR`, `CMAKE_FIND_ROOT_PATH_MODE_PROGRAM`, and
    `CMAKE_CROSSCOMPILING_EMULATOR`, each only when set.
  - It builds, then runs the program, through the emulator when one is
    set. It fails with `message(FATAL_ERROR)` naming the step that
    failed: configure, build, target check, or run.
  - The target check (SC-003): it lists the consumer build's targets
    with `<make program> -t targets all` and fails on any Overdrive
    test, example, or benchmark target. The names come from
    `tests/CMakeLists.txt`, `examples/`, and `bench/`.
  - For `duplicate`, success is a failed configure whose output contains
    the R203 message.
  - Every external command runs with `execute_process` and an argument
    list, never a shell string (Art. XIII).
- [ ] T203 [US1] Register `consumer_vendored` and `consumer_duplicate` in
  `tests/CMakeLists.txt`, each running `run_consumer.cmake` with
  `TIMEOUT 300`. Run them and watch both fail on today's
  `CMakeLists.txt`:
  - `consumer_vendored` at the target check, since the tests default
    ON;
  - `consumer_duplicate` without the R203 message.

  Record both failures for the PR. (Depends on T201, T202.)
- [ ] T204 [US1] In `CMakeLists.txt`:
  - `option(OVERDRIVE_BUILD_TESTS ... ${PROJECT_IS_TOP_LEVEL})` (R201);
  - set `CMAKE_RUNTIME_OUTPUT_DIRECTORY` only when
    `PROJECT_IS_TOP_LEVEL`, and compare `PROJECT_SOURCE_DIR` with
    `PROJECT_BINARY_DIR` in the in-source check (R202);
  - add the R203 global-property guard before `add_library(overdrive
    ...)`. Its message reads "Overdrive was added twice: first from
    <dir>, again from <dir>".

  Check R202's ASSUMED directory-scope rule and R203's ASSUMED global
  property against the CMake documentation, and quote both in the PR.
  (Depends on T203.)
- [ ] T205 [US1] In `CMakeLists.txt`, replace the `BUILD_INTERFACE`
  include directory and the private `target_sources` of the public
  headers with `target_sources(overdrive PUBLIC FILE_SET HEADERS
  BASE_DIRS include FILES ...)`, built from `overdrive_public_headers`
  (R204). The header checks in `tests/CMakeLists.txt` must still pass
  unchanged. (Depends on T204.)
- [ ] T206 [US1] Proof: `debug`, `debug-clang`, `shared`, `sanitize`,
  `release`, `windows-cross-debug`, and `windows-cross-shared` from
  clean, each with 0 warnings and all tests passing, including the two
  new ones. (Depends on T205.)

## Phase 2 — OVD-23 Installed route (US2, US3; depends on OVD-22)

Goal: install once, then find the package by name, from a moved prefix
(US2). CI runs both routes everywhere (US3). Independent test:
`ctest --preset debug -R '^consumer_installed$'`.

- [ ] T207 [US2] Extend `tests/consumer/CMakeLists.txt` with the route
  `installed`, which calls `find_package(overdrive 0.1 REQUIRED)`.
  Extend `run_consumer.cmake` with `-DMODE=installed`, in this order:
  1. Run `cmake --install` on the outer build tree to `<work>/a
     b/prefix`. The space is deliberate.
  2. List the prefix. Fail on any file outside R211's set, and on any
     required file that is missing: each public header, the library,
     the three package files, `LICENSE`, and `LICENSE-UCD` (FR-009,
     FR-010).
  3. Search every installed text file under `<libdir>/cmake/overdrive`
     for the outer source and build directories (R206).
  4. Rename the prefix to `<work>/a b/moved`.
  5. Configure the consumer with `CMAKE_PREFIX_PATH` set to the moved
     prefix, then build and run it.
  6. Configure once more, requesting version 0.2, and require a failed
     configure that names both versions (US2 scenario 4).

  Register `consumer_installed` with `TIMEOUT 300`. Watch it fail at
  step 1 or 2 on today's tree, and record the failure. (Depends on
  OVD-22.)
- [ ] T208 [US2] In `CMakeLists.txt`, add the install rules (R205):
  - `include(GNUInstallDirs)` and `include(CMakePackageConfigHelpers)`;
  - `install(TARGETS overdrive EXPORT overdrive-targets RUNTIME LIBRARY
    ARCHIVE FILE_SET HEADERS INCLUDES DESTINATION
    ${CMAKE_INSTALL_INCLUDEDIR})`;
  - `install(EXPORT overdrive-targets NAMESPACE overdrive:: DESTINATION
    ${CMAKE_INSTALL_LIBDIR}/cmake/overdrive)`;
  - `configure_package_config_file` from the new
    `cmake/overdrive-config.cmake.in`, which holds `@PACKAGE_INIT@`,
    the include of `overdrive-targets.cmake`, and
    `check_required_components(overdrive)`;
  - `write_basic_package_version_file(... COMPATIBILITY
    SameMinorVersion)`;
  - install both generated files beside the targets file.

  Check R205's ASSUMED lowercase config name and R206's ASSUMED
  prefix-relative targets file against the documentation, and quote
  them in the PR. (Depends on T207.)
- [ ] T209 [P] [US2] In `CMakeLists.txt`, install `LICENSE` and
  `third_party/ucd/LICENSE` (renamed to `LICENSE-UCD`) to
  `${CMAKE_INSTALL_DOCDIR}` (R207). Check that `CMAKE_INSTALL_DOCDIR`
  defaults to `share/doc/overdrive`. This shares the install section
  with T208; land it in the same commit series. (Depends on T207.)
- [ ] T210 [US3] Show that the check works: temporarily drop one header
  from the file set, watch `consumer_installed` fail at the file-list or
  build step, then restore it. Record the output for the PR. (Depends
  on T208, T209.)
- [ ] T211 [US3] Proof:
  - the T206 preset list from clean, with 0 warnings;
  - the PR's CI run, showing `consumer_vendored`, `consumer_duplicate`,
    and `consumer_installed` passing in every job, Linux, macOS, and
    both Wine rows. Cite the run id.

  Check R208's ASSUMED rule (a test's `TIMEOUT` overrides the preset
  default) and R209's empty-copy behavior. (Depends on T210.)

## Phase 3 — OVD-24 Documentation (depends on OVD-23)

- [ ] T212 [P] Add a "Using Overdrive" section to `README.md`:
  - both routes, two lines of CMake each, as in
    `contracts/consumer.md`;
  - the defaults when vendored;
  - the version rule for 0.x;
  - the Windows DLL step;
  - a pointer to the contract.

  State only what the consumer tests show (Art. XV). (FR-015)
- [ ] T213 [P] Add a short "Packaging" section to `docs/architecture.md`
  (the two routes, the header file set as the single list, and the
  consumer tests). In `docs/compatibility.md`, add the consumer tests
  to "What these runs show".

## Dependencies

- OVD-22 → OVD-23 → OVD-24, strictly in order. OVD-22 and OVD-23 both
  edit `CMakeLists.txt`, `tests/CMakeLists.txt`, and the consumer
  files. OVD-24 documents what they prove.
- Inside a phase: the test tasks (T201–T203, T207) come before the
  build changes (T204, T205, T208, T209). Proof (T206, T211) comes last.

## Parallel examples

- **Phase 2**: T208 and T209 are separate install rules, written
  together after T207.
- **Phase 3**: T212 and T213 touch different files.

## Implementation strategy

- **MVP: OVD-22 alone.** It is everything the task manager needs to
  vendor Overdrive. After it merges, the task manager can adopt
  Overdrive in its own repository while OVD-23 proceeds.
- **OVD-23** adds the installed route and its CI proof, for later
  applications and for the owner's request.
- **OVD-24** documents both routes, once both are proved.
