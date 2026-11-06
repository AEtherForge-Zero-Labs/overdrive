# Overdrive

**Every frame matters.**

Overdrive is a high-performance terminal UI framework
written in C23.

Build composable, colorful, animated terminal applications
with a native rendering engine designed for explicit
control and predictable performance.

## Status

**The rendering core, input, and abnormal-exit restoration are
implemented. Version 0.1.0 is experimental: every 0.x interface may
change.** Feature 1,
[`specs/001-render-core/`](specs/001-render-core/), provides the
following:

- terminal lifecycle:
  - the process's own console (the system backend). POSIX and Windows
    implementations exist. The owner has seen both work: WezTerm on
    macOS, and Command Prompt on Windows 11 (see
    [`docs/compatibility.md`](docs/compatibility.md));
  - a headless backend that delivers output to a callback, for tests;
- a terminal-cell canvas with truecolor styles and Unicode-aware widths;
- drawing with clipping: fill, text, and boxes in five border styles;
- differential rendering: each frame writes only what changed, in one
  write.

Measured frame costs, with the machine and command, are in
[`docs/architecture.md`](docs/architecture.md#measurements). What has
been observed in real terminals is in
[`docs/compatibility.md`](docs/compatibility.md).

Feature 2, [`specs/002-input-restore/`](specs/002-input-restore/),
provides the following:

- **Input.** It reads keys with modifiers, mouse reports, bracketed
  paste, focus, and resizes. Events are decoded from untrusted bytes, and
  `ovd_input_wait` blocks until the next event or a timeout. On the
  headless backend, `ovd_input_feed` supplies the bytes.
- **Abnormal-exit restoration.** When the process ends through a signal,
  a crash, or a closed console, the terminal is restored as destroy would
  restore it. It is a create option, on by default.
- **Process-wide handlers.** A system terminal with restoration on
  installs process-wide signal and console handlers. On POSIX, input on
  also installs a `SIGWINCH` handler for resizes. This is the one
  exception to keeping no process-wide state (FR-036). Headless
  terminals install none.
- **Not covered.** Some ends can't be restored: `SIGKILL`, power loss,
  and `exit` without destroy, among others. FR-034 in the
  [spec](specs/002-input-restore/spec.md) lists them, and
  `terminal.h` repeats the list at `restore_on_exit`.

The pty tests show restoration on Linux and macOS. On Windows, CI
only cross-builds input and restoration: Wine runs only the headless
tests and the redirected-stdout path. On Windows 11, the owner
reported that closing the window "seemed fine". See
[`docs/compatibility.md`](docs/compatibility.md). Layout, motion, and
widgets are later features, each specified when a consumer needs it.

Feature 3, [`specs/003-packaging/`](specs/003-packaging/), lets another
CMake project build and link Overdrive, either from its source tree or
from an installed copy. See [Using Overdrive](#using-overdrive).

## Build

The CMake presets are the supported build. Minimum tool versions are in
[`.specify/memory/baseline.md`](.specify/memory/baseline.md).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The `bench` preset builds the render benchmark (`render_benchmark`).

## Using Overdrive

A CMake project gets Overdrive in one of two ways. Both give the same
target, `overdrive::overdrive`: a static library, or a shared one when
`BUILD_SHARED_LIBS` is on. The full build contract is
[`specs/003-packaging/contracts/consumer.md`](specs/003-packaging/contracts/consumer.md).

**From the source tree.** Copy or fetch Overdrive into your project,
then:

```cmake
add_subdirectory(third_party/overdrive)
target_link_libraries(app PRIVATE overdrive::overdrive)
```

- **Defaults.** When Overdrive is not the top-level project, its tests,
  examples, benchmarks, and install rules are off. To turn one on, set
  `OVERDRIVE_BUILD_TESTS`, `OVERDRIVE_BUILD_EXAMPLES`,
  `OVERDRIVE_BUILD_BENCHMARKS`, or `OVERDRIVE_INSTALL` before
  `add_subdirectory`.
- **What linking adds.** C23, and `OVERDRIVE_SHARED` when the library is
  shared. Nothing else reaches your targets: no compile options and no
  other definitions.
- **Cache entries.** Overdrive adds none outside `OVERDRIVE_*`,
  `overdrive_*`, and `BUILD_SHARED_LIBS`.
- **Refusals.** A second copy of Overdrive in the same build, or a target
  of yours named `overdrive`, stops configuration with a message naming
  the cause.

**From an installed copy.** Install a build of Overdrive once:

```bash
cmake --install <overdrive-build-dir> --prefix <prefix>
```

Then, with `<prefix>` in `CMAKE_PREFIX_PATH`:

```cmake
find_package(overdrive 0.1 REQUIRED)
target_link_libraries(app PRIVATE overdrive::overdrive)
```

- **What the prefix holds.** The public headers, the library, the
  package files, and Overdrive's `LICENSE` and the Unicode data notice
  `LICENSE-UCD` in `share/doc/overdrive`. Nothing else.
- **Moving it.** The prefix can be moved after installing.

**Versions.** While the major version is 0, `find_package(overdrive
0.1)` accepts only 0.1.x: an installed 0.2 or 0.0 is refused. A request
without a version accepts any installed version.

**Windows, shared.** The program needs the DLL beside it or on `PATH`.
This step copies it after each build:

```cmake
if(WIN32)
    add_custom_command(TARGET app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t
            $<TARGET_FILE_DIR:app> $<TARGET_RUNTIME_DLLS:app>
        COMMAND_EXPAND_LISTS)
endif()
```

With a static library the DLL list is empty, and the step copies
nothing.

Each claim above is checked by the consumer tests, `consumer_vendored`,
`consumer_duplicate`, and `consumer_installed`, in every CI job. They
build a separate minimal project with `-Wall -Wextra -Wpedantic -Werror`
(see [`docs/compatibility.md`](docs/compatibility.md)).

## Usage

```c
#include <overdrive/overdrive.h>
```

The API is in [`include/overdrive/`](include/overdrive/):
- `core.h`: results, sizes, rects, version;
- `terminal.h`: terminals, configuration, allocator;
- `render.h`: colors, styles, frames, drawing;
- `input.h`: events, wait, feed.

The examples, built with `OVERDRIVE_BUILD_EXAMPLES`, are
`hello_overdrive` (rendering) and `input_demo` (input and restoration).
[`docs/validation/manual-checklist.md`](docs/validation/manual-checklist.md)
says how to check them by hand in a real terminal.

## Contributing

Read [`AGENTS.md`](AGENTS.md) and the
[constitution](.specify/memory/constitution.md). Naming and branding follow
[`OVD-IDENTITY-001`](docs/identity/OVD-IDENTITY-001.md).

## License

Copyright AEtherForge Zero Labs. All rights reserved until a license is
chosen; see [LICENSE](LICENSE).
