# Overdrive Compatibility

A compatibility claim names the terminal, its version, and the platform on
which the behavior was observed (Art. XV). This file records two things
separately:
- what has been observed in real terminals;
- what automated tests show, which is not a terminal observation.

## Terminals observed

Agents cannot make these observations (Art. XVI), so the owner records
them through the manual validation checklist,
[`validation/manual-checklist.md`](validation/manual-checklist.md):
part 1 for rendering, part 2 for input and restoration. Each row
added here names:
- the terminal and its version;
- the operating system and its version;
- the date;
- the checklist result.

| Terminal | Version | Platform | Date | Result |
|---|---|---|---|---|
| WezTerm | 20250723-081524-efec7001 | macOS 27.2 (26B5091g) | 2026-11-05 | Owner's summary: works. |
| Command Prompt | not recorded | Windows 11, version not recorded; a `windows-cross-debug` build | 2026-11-05 | Owner's summary: works, and closing the window restored the console. |

Both rows are summaries: the owner reported that the program works,
but did not record a result for each check. The Windows row does not
record which window hosted Command Prompt. Windows 11 opens it in
Windows Terminal or in the console host, depending on its
default-terminal setting.

In those two terminals, the behaviors below were seen working as a
whole. For any other terminal, and for each check on its own, they
rest on cited documentation and are unverified (research §E,
"Unverified"):
- the enter and exit sequences (R26);
- deferred wrap at the last column (R33);
- the alternate screen (mode 1049);
- synchronized output (mode 2026, R34);
- the Windows console modes (R28).

The same holds for input and restoration (feature 002):
- what each terminal sends for keys, mouse, paste, and focus;
- whether it honors the mouse, paste, and focus modes;
- restoration after closing the window;
- on Windows, the console input path, the console control handler, the
  unhandled-exception filter, and the `SIGABRT` handler (R113). They
  ran in the Windows row above, but the exit statuses they produced
  (3 for `abort`, 0xC0000005, 0xC000013A) were not recorded.

## Automated platforms

CI runs the test suite on every pull request (`.github/workflows/ci.yml`).
The versions below are the ones logged by CI run 42102099561 on
2026-11-05, the run in which every job passed on the reviewed head of the
input example's pull request. Runner images move, and `macos-latest` in
particular follows GitHub's image (see `baseline.md`), so later runs may
use newer versions.

| Platform (runner image) | Toolchain | Presets |
|---|---|---|
| Linux, `ubuntu-24.04` | gcc 14.2.0 (Ubuntu 14.2.0-4ubuntu2~24.04.1), CMake 3.31.6 | `debug`, `release`, `shared`, `sanitize` |
| Linux, `ubuntu-24.04` | clang 19.1.1 (Ubuntu 1ubuntu1~24.04.2), CMake 3.31.6 | `debug-clang` |
| macOS, `macos-26-arm64` | Apple clang 21.0.0 (clang-2100.1.1.101), CMake 4.4.4 (`sanitize`: 4.4.5) | `debug`, `shared`, `sanitize` |
| Windows, cross-built and run under Wine in a `debian:trixie` container on `ubuntu-24.04` | `x86_64-w64-mingw32-gcc` 14-win32, Wine 10.0 (Debian 10.0~repack-6), CMake 3.31.6 | `windows-cross-debug`, `windows-cross-shared` |

What these runs show:
- **The headless backend.** Rendering is tested headless. The diff tests
  (`test_diff`) replay the output bytes through an interpreter that is
  stricter than any terminal (R38). That shows which bytes Overdrive
  emits. It does not show how a terminal displays them.
- **The system backend's refused path.** With stdout redirected to a
  file, create returns `OVD_ERROR_NOT_A_TERMINAL` and writes nothing
  (`test_system_notty`, every platform above).
- **Input on the headless backend.** These tests ran in every job
  above, Wine included:
  - the decoder's table (`test_decode`, SC-001);
  - 10⁷ fuzzed bytes (`test_decode_fuzz`, SC-002; under the sanitizers in
    the `sanitize` jobs);
  - wait, feed, and the virtual clock (`test_input`);
  - 0 allocator calls while reading input (`test_steady_state`, SC-004).
- **The system backend on a pseudo-terminal.** This applies to Linux
  and macOS, every job above. These tests are POSIX-only, so Wine does
  not run them.
  - `test_system_pty` drives create, frames, resizes, and input through a
    pty: raw mode and its restoration, keys, `SIGWINCH` resizes, the
    timing bounds of SC-005, and refused allocations.
  - `test_restore_pty` ends forked children with each signal of FR-030.
    Each time, it checks that the pty's modes equal those before create,
    that the exit sequence was written once, and that the child's exit
    status is the same as without Overdrive (SC-003). It also covers:
    - the application's own handlers, before and after create;
    - an ignored `SIGHUP`;
    - `restore_on_exit` off;
    - a second system terminal;
    - destroy putting the handlers back;
    - create and destroy in a loop under `SIGTERM`.
- **One case is skipped on macOS.** A local run on macOS (Darwin 27.2.0)
  printed it: "SA_RESETHAND crash handler returns" is SKIPPED, because
  the platform does not report the flag (R121, FR-034). On Linux the test
  fails instead of skipping that case. The Linux jobs of CI run
  42140940176 on 2026-11-05 passed with that check in place. That shows
  the case runs on Linux, and that Linux reports the flag. Any later
  Linux job that passes with this check in place shows the same.
- **A pty is not a terminal.** The pty tests show the bytes and modes
  Overdrive produces. They don't show how a terminal emulator decodes
  keys or displays the result.
- **Wine is not Windows.** It runs the headless suite and the
  redirected-stdout path only. It runs none of the Windows console
  input or restoration code. It is not evidence of behavior in Windows
  Terminal or the Windows console (`baseline.md`).
