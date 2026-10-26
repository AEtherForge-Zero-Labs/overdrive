# Overdrive

**Every frame matters.**

Overdrive is a high-performance terminal UI framework
written in C23.

Build composable, colorful, animated terminal applications
with a native rendering engine designed for explicit
control and predictable performance.

## Status

**Design phase. Nothing is implemented yet.** No library, headers, or
build exist in this repository today. Everything below describes planned
capabilities, not present ones.

The first feature, the rendering core, is specified in
[`specs/001-render-core/`](specs/001-render-core/):

- terminal lifecycle with a headless backend for tests
- a terminal-cell buffer with truecolor styles and Unicode-aware widths
- drawing with clipping: fill, text, boxes
- differential rendering: each frame writes only what changed

Layout, motion, widgets, and input are later features. They will be
specified when a consumer needs them.

## Planned usage

```c
#include <overdrive/overdrive.h>
```

```cmake
target_link_libraries(myapp PRIVATE overdrive::overdrive)
```

The proposed API is in
[`specs/001-render-core/contracts/`](specs/001-render-core/contracts/include/overdrive/).
All 0.x interfaces are experimental.

## Contributing

Read [`AGENTS.md`](AGENTS.md) and the
[constitution](.specify/memory/constitution.md). Naming and branding follow
[`OVD-IDENTITY-001`](docs/identity/OVD-IDENTITY-001.md).

## License

Copyright AEtherForge Zero Labs. All rights reserved until a license is
chosen; see [LICENSE](LICENSE).
