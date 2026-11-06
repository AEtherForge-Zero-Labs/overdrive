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
[`docs/architecture.md`](docs/architecture.md#measurements). No real
terminal has been observed yet; see
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
tests and the redirected-stdout path. The owner has seen them work on
Windows 11 by hand. See [`docs/compatibility.md`](docs/compatibility.md). Layout, motion, and
widgets are later features, each specified when a consumer needs it.

## Build

The CMake presets are the supported build. Minimum tool versions are in
[`.specify/memory/baseline.md`](.specify/memory/baseline.md).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The `bench` preset builds the render benchmark (`render_benchmark`).

## Usage

```c
#include <overdrive/overdrive.h>
```

There are no install rules, so add Overdrive to your build as a
subdirectory:

```cmake
set(OVERDRIVE_BUILD_TESTS OFF)
add_subdirectory(overdrive)
target_link_libraries(myapp PRIVATE overdrive::overdrive)
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
