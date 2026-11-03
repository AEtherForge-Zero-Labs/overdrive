## Why

Issue #7 (OVD-06) supplies the two pieces the system backend (OVD-11)
needs underneath it: a platform layer that changes and restores the
operating-system terminal modes (FR-010, FR-011; research R11, R25–R28),
and color-depth detection from the environment (FR-013; R10, R36). Both
must exist and be checked against their vendor documentation before
OVD-11 wires them into `ovd_terminal_create` and `ovd_terminal_destroy`.

## What Changes

- Add the private header `src/platform/platform.h`: plain functions with
  no OS types (R11). It covers whether stdout is a terminal, entering and
  leaving the operating-system modes (R28 steps 2–4) with a record of the
  prior values, reading the size, writing every byte, and looking up an
  environment variable.
- Add `src/platform/platform_posix.c`: termios `ECHO` on stdin only if it
  is a terminal (R27), `ioctl(TIOCGWINSZ)`, and a write loop that retries
  partial writes and `EINTR`.
- Add `src/platform/platform_windows.c`: output mode with
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING` and `DISABLE_NEWLINE_AUTO_RETURN`,
  output code page 65001, input `ENABLE_ECHO_INPUT` off,
  `GetConsoleScreenBufferInfo`, and a `WriteFile` loop.
- Add `src/detect.h` and `src/detect.c`: R10 color detection,
  platform-independent, through an injected environment lookup.
- Add `tests/test_detect.c` and register it. It tests every R10/R36 rule
  through a fake environment, and the real lookup through CTest's
  `ENVIRONMENT` property.
- Append the platform file for the target and `src/detect.c` to the
  library sources.
- Record the choices the spec leaves open as R40 in `research.md`. Tick
  T020–T023.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements remain in Spec Kit. This transaction
implements T020–T023 without changing their requirements.

### Modified Capabilities

None.

## Impact

No public header changes. Nothing calls the platform layer yet: OVD-11
wires it in. Only the paths owned by issue #7, two appended blocks in
the shared CMake files, the R40 decision entry, the T020–T023
checkboxes, and this record change.
