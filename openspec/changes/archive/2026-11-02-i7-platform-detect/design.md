## Context

R11 fixes the shape: one private header, plain functions, one platform
file per build chosen by CMake, and no OS types in the header. R28 fixes
the create order. Steps 2–4 (Windows output mode, Windows code page,
input echo) are operating-system modes, so they belong here. Steps 1
(stdout is a terminal) and 5 (size) are queries, so they are separate
functions. Steps 6 and 7 (allocate, enter sequence) are OVD-11's. R10
and R36 fix detection, and R35 makes detection system-only.

## Goals / Non-Goals

**Goals:** every OS call checked against POSIX or Microsoft documentation
and cited at the call. Enter is all-or-nothing, with rollback inside it.
Leave is best-effort and idempotent. Detection is deterministic and
testable without touching the process environment.

**Non-Goals:** the enter/exit VT sequences (R26, OVD-11); the config
override of detection (OVD-11/OVD-10); reading input; resize signals;
any pre-1809 Windows fallback (R28).

## Decisions

- **Record type.** `OvdPlatformModes` holds a `uint32_t` set of
  completed steps and an aligned 256-byte area. Each platform file
  stores its own saved values there with `memcpy` and `static_assert`s
  that they fit: `struct termios` on POSIX; output mode, code page, and
  input mode on Windows. This keeps OS types out of the header (R11) and
  keeps allocation out of the platform layer (R32: only create and a
  resizing begin allocate). Zero-initialized means "nothing recorded".
  *Rejected:* an opaque struct with a size query (forces an allocation
  or a VLA in OVD-11); saving only `c_lflag` (R26 says termios is
  restored to the values read).
- **Enter does R28 steps 2–4, in order, and undoes its own steps on
  failure.** It returns `OVD_ERROR_PLATFORM` with nothing recorded and
  nothing left changed, or `OVD_OK` with each completed step recorded.
  Given a record that already holds steps, it returns
  `OVD_ERROR_INVALID_ARGUMENT`. Overwriting that record would lose the
  values to restore.
- **Leave undoes the recorded steps in reverse (4, 3, 2) and attempts
  every one even if one fails (R28).** It then clears the record, so a
  second leave does nothing.
- **POSIX echo.** Echo changes only if `isatty(STDIN_FILENO)`; otherwise
  the step is skipped, which is not an error (R27). `tcgetattr` records
  the termios, and `tcsetattr(TCSANOW)` clears `ECHO`. POSIX says
  `tcsetattr` "shall return successfully if it was able to perform any
  of the requested actions", so enter re-reads with `tcgetattr` and
  fails, restoring, if `ECHO` is still set. `TCSANOW`, not `TCSAFLUSH`:
  pending input is the user's, and nothing here reads it. `EINTR` from
  `tcsetattr` (a listed error) is retried.
- **POSIX size.** `ioctl(STDOUT_FILENO, TIOCGWINSZ)`, as T021 says.
  POSIX Issue 8's `tcgetwinsize` is absent from the macOS SDK (checked:
  no declaration in its `termios.h`), so the ioctl is the portable
  form. `TIOCGWINSZ` is documented by the Linux man-pages project
  (`TIOCGWINSZ(2const)`), and the macOS SDK defines it in
  `sys/ttycom.h`. A zero row or column count is `OVD_ERROR_PLATFORM`
  (R25).
- **POSIX write.** Loop on `write(STDOUT_FILENO)`. `EINTR` before any
  byte is retried, and a short count continues from where it stopped.
  Any other error, or a zero return for a non-zero request (POSIX
  defines none), is a failure, so the loop cannot spin. Each call asks
  for at most `SSIZE_MAX` bytes (above that, POSIX leaves the result
  implementation-defined).
- **Windows output mode.** The mode is read with `GetConsoleMode` and
  set with `SetConsoleMode` to the read value OR
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN |
  ENABLE_PROCESSED_OUTPUT`. The last flag goes beyond R28's two because
  Microsoft's `SetConsoleMode` reference says, at
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING`, "Ensure ENABLE_PROCESSED_OUTPUT
  is set when using this flag". It is on by default, and leave restores
  the whole mode, so this is recorded as an assumption in R40.
- **Windows code page.** `GetConsoleOutputCP` (0 means failure) is
  recorded, then `SetConsoleOutputCP(CP_UTF8)` (65001).
- **Windows echo.** If `GetConsoleMode` succeeds on the input handle,
  stdin is a console: clear `ENABLE_ECHO_INPUT`. Otherwise skip the
  step (R27). Clearing echo while `ENABLE_LINE_INPUT` stays set is
  allowed; the reverse is not (echo "can be used only if
  ENABLE_LINE_INPUT is also enabled").
- **Windows "is a terminal".** `GetStdHandle(STD_OUTPUT_HANDLE)` is
  neither `INVALID_HANDLE_VALUE` nor `NULL` (both documented failure
  returns), and `GetConsoleMode` succeeds on it. That `GetConsoleMode`
  fails for a redirected handle is INFERRED: its reference requires "a
  handle to the console input buffer or the console screen buffer" but
  does not state the failure. The manual checklist (OVD-12) covers it.
- **Windows size.** `GetConsoleScreenBufferInfo`, `srWindow`:
  `Right - Left + 1` columns, `Bottom - Top + 1` rows. That the corners
  are inclusive is INFERRED from `SMALL_RECT`'s "coordinates specify the
  rows and columns of screen-buffer character cells". A non-positive
  result is `OVD_ERROR_PLATFORM` (R25).
- **Windows write.** Loop on `WriteFile`, at most `UINT32_MAX` bytes per
  call (`DWORD`). The documentation allows `TRUE` with fewer bytes than
  requested on a non-blocking pipe, so the loop continues. `FALSE`, or
  `TRUE` with zero bytes, is a failure.
- **Environment lookup.** `ovd_platform_environment(context, name)` is
  ISO C `getenv` behind the same signature as detection's lookup. OVD-11
  can pass it directly; the context is unused. It is implemented in each
  platform file, because only one is compiled per build.
- **Detection.** `ovd_detect_color_depth(lookup, context,
  virtual_terminal)` applies R10 first-match in R36's order:
  `NO_COLOR` present and non-empty → NONE; `COLORTERM` exactly
  `truecolor` or `24bit` → TRUECOLOR; `virtual_terminal` → TRUECOLOR;
  `TERM` contains `256color` → 256; `TERM` exactly `dumb` → NONE;
  otherwise 16. Matching is case-sensitive (R36 says exactly). A nullptr
  lookup is an empty environment. `virtual_terminal` comes from
  `ovd_platform_virtual_terminal(&modes)`: true only if the Windows
  output-mode step is recorded. That keeps `#ifdef _WIN32` out of
  `src/detect.c` and `src/terminal_system.c`.
- **Naming.** Every non-static name is `ovd_platform_…` or
  `ovd_detect_…`, because the export check also runs on the static
  archive (see #28). The steps are a file-local enum with a fixed
  underlying type (Art. III). The set is a `uint32_t`, the same tension
  as `OvdAttr` (research §A).
- **Feature macros.** `platform_posix.c` defines
  `_POSIX_C_SOURCE 200809L` before any include, because the build uses
  `-std=c23` without extensions. `platform_windows.c` defines
  `WIN32_LEAN_AND_MEAN`.

## Risks / Trade-offs

- [Nothing exercises enter/leave in CTest. OVD-11 tests only the
  not-a-terminal path, and this issue owns no platform test.] →
  Verified once by a scratch harness under a pseudo-terminal on macOS
  (`script(1)`), with the output in the PR. Windows behavior is
  UNVERIFIED and goes to the OVD-12 manual checklist.
- [`tcsetattr` from a background process group raises `SIGTTOU`, which
  stops the process (POSIX).] → That is standard job-control behavior.
  Blocking the signal would be process-wide state (FR-002), so it is
  documented, not changed.
- [Wine is not Windows (baseline).] → The cross build proves compilation
  and the headless detection test only.
