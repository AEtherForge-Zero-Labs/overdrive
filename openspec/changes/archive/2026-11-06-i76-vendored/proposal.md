## Why

The first consumer, the owner's task manager, will add Overdrive's source
tree as a subproject. Today that would build Overdrive's tests inside the
consumer's build, and a second copy would fail with a duplicate-target
error that does not say why. Feature 003 (`specs/003-packaging/`) plans
the vendored route; this change implements it (OVD-22, tasks T201–T206).

## What Changes

- A minimal consumer project in `tests/consumer/` (R209). It has the
  routes `vendored`, `duplicate`, and `collision`, checks the usage
  requirements, the cache diff, and the target allowlist, and runs a
  headless frame through the public API.
- `tests/consumer/run_consumer.cmake` configures, builds, and runs the
  consumer with the outer build's settings (R208).
- Two CTest tests, `consumer_vendored` and `consumer_duplicate`, with
  `TIMEOUT 300`.
- `CMakeLists.txt`: tests default to `PROJECT_IS_TOP_LEVEL` (R201); the
  runtime output directory is set only at top level, and the in-source
  check uses `PROJECT_*` (R202); a guard refuses a second copy or a
  consumer target named `overdrive` (R203); the public headers become a
  `PUBLIC` header file set (R204).

## Capabilities

### New Capabilities

None. The behavior is specified in `specs/003-packaging/`, not in
OpenSpec capability specs.

### Modified Capabilities

None.

## Impact

`CMakeLists.txt`, `tests/CMakeLists.txt`, and the new `tests/consumer/`
files. No public header or C source changes. Overdrive's own builds keep
their behavior: the presets set every option explicitly.
