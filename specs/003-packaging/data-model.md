# Data Model: Packaging

This feature adds no runtime data. Its entities are build artifacts.

## Consumer project

- **Owns**: its warning flags, output directories, test settings, and
  the C standard of its own targets.
- **Chooses**: a route (vendored or installed), and static or shared
  through `BUILD_SHARED_LIBS`.
- **Uses**: `overdrive::overdrive` only (contract: `contracts/consumer.md`).

## Vendored copy

- **Identity**: the absolute source directory of the copy.
- **Invariant**: at most one copy per build. The first copy records its
  directory in a global property, and a second copy stops configuration
  naming both directories (R203).
- **Defaults**: tests off when not top-level (R201).

## Installed package

- **Fields**: version (from `project(VERSION)`); compatibility
  `SameMinorVersion` (R205); variant (static or shared, fixed at build
  time); the file list in `contracts/consumer.md`.
- **Invariants**: no file outside the contract's list (R211); no
  absolute path of the build machine (R206).

## Minimal consumer

- **Fields**: route (`vendored` | `installed`), and either Overdrive's
  source directory or the install prefix.
- **States**: configured → built → ran. The test fails at the first
  state not reached, and names it (R208).
