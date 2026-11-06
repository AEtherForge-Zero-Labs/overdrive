## Why

Feature 003 (`specs/003-packaging/`) gives consumers two routes to
Overdrive. The vendored route landed in OVD-22. This change adds the
installed route (OVD-23, tasks T207–T211): install once, then
`find_package(overdrive)` from any prefix, including a moved one.

## What Changes

- `OVERDRIVE_INSTALL`, defaulting to `PROJECT_IS_TOP_LEVEL` (R212). It
  gates the install rules: headers through the file set, the library,
  the export with namespace `overdrive::`, the package config and
  version files (`SameMinorVersion`), and `LICENSE` and `LICENSE-UCD` in
  `${CMAKE_INSTALL_DATAROOTDIR}/doc/overdrive` (R205–R207).
- The new `cmake/overdrive-config.cmake.in`.
- The consumer gains the route `installed`, and its own install rule.
- `run_consumer.cmake` gains mode `installed`:
  - install to a path with a space;
  - check the file list and the license hashes;
  - search the package files for build paths;
  - move the prefix and find the package there only;
  - build and run;
  - check that 0.2 and 0.0 are refused.

  Mode `vendored` gains the FR-016 check.
- The CTest test `consumer_installed`, registered when the install
  rules are on.

## Capabilities

### New Capabilities

None. The behavior is specified in `specs/003-packaging/`.

### Modified Capabilities

None.

## Impact

`CMakeLists.txt`, `cmake/overdrive-config.cmake.in`,
`tests/CMakeLists.txt`, `tests/consumer/CMakeLists.txt`, and
`tests/consumer/run_consumer.cmake`. No C source or public header
changes.
