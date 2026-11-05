# Overdrive

**Every frame matters.**

Overdrive is a high-performance terminal UI framework
written in C23.

Build composable, colorful, animated terminal applications
with a native rendering engine designed for explicit
control and predictable performance.

## Status

**The rendering core is implemented. Version 0.1.0 is experimental:
every 0.x interface may change.** Feature 1,
[`specs/001-render-core/`](specs/001-render-core/), provides the
following:

- terminal lifecycle:
  - the process's own console (the system backend). POSIX and Windows
    implementations exist; neither has been observed in a real terminal
    yet;
  - a headless backend that delivers output to a callback, for tests;
- a terminal-cell canvas with truecolor styles and Unicode-aware widths;
- drawing with clipping: fill, text, and boxes in five border styles;
- differential rendering: each frame writes only what changed, in one
  write.

Measured frame costs, with the machine and command, are in
[`docs/architecture.md`](docs/architecture.md#measurements). No real
terminal has been observed yet; see
[`docs/compatibility.md`](docs/compatibility.md).

Layout, motion, widgets, and input are later features. Each will be
specified when a consumer needs it.

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
- `render.h`: colors, styles, frames, drawing.

## Contributing

Read [`AGENTS.md`](AGENTS.md) and the
[constitution](.specify/memory/constitution.md). Naming and branding follow
[`OVD-IDENTITY-001`](docs/identity/OVD-IDENTITY-001.md).

## License

Copyright AEtherForge Zero Labs. All rights reserved until a license is
chosen; see [LICENSE](LICENSE).
