# Overdrive Compatibility

A compatibility claim names the terminal, its version, and the platform on
which the behavior was observed (Art. XV). This file records two things
separately:
- what has been observed in real terminals;
- what automated tests show, which is not a terminal observation.

## Terminals observed

**None yet.** No terminal emulator or console has been observed running
Overdrive. Agents cannot make these observations (Art. XVI), so the owner
records them through the manual validation checklist (OVD-12). Each row
added here names:
- the terminal and its version;
- the operating system and its version;
- the date;
- the checklist result.

| Terminal | Version | Platform | Date | Result |
|---|---|---|---|---|

Until a row exists, these behaviors rest on cited documentation and are
unverified (research §E, "Unverified"):
- the enter and exit sequences (R26);
- deferred wrap at the last column (R33);
- the alternate screen (mode 1049);
- synchronized output (mode 2026, R34);
- the Windows console modes (R28).

## Automated platforms

CI runs the test suite on every pull request (`.github/workflows/ci.yml`).
The versions below are the ones logged by CI run 41974704883 on
2026-11-05. Runner images move, and `macos-latest` in particular follows
GitHub's image (see `baseline.md`), so later runs may use newer versions.

| Platform (runner image) | Toolchain | Presets |
|---|---|---|
| Linux, `ubuntu-24.04` | gcc 14.2.0 (Ubuntu 14.2.0-4ubuntu2~24.04.1), CMake 3.31.6 | `debug`, `release`, `shared` |
| Linux, `ubuntu-24.04` | clang 19.1.1 (Ubuntu 1ubuntu1~24.04.2), CMake 3.31.6 | `debug-clang` |
| macOS, `macos-26-arm64` | Apple clang 21.0.0 (clang-2100.1.1.101), CMake 4.4.4 | `debug`, `shared` |
| Windows, cross-built and run under Wine in a `debian:trixie` container on `ubuntu-24.04` | `x86_64-w64-mingw32-gcc` 14-win32, Wine 10.0 (Debian 10.0~repack-6), CMake 3.31.6 | `windows-cross-debug`, `windows-cross-shared` |

What these runs show:
- **The headless backend.** Rendering is tested headless. The diff tests
  (`test_diff`) replay the output bytes through an interpreter that is
  stricter than any terminal (R38). That shows which bytes Overdrive
  emits. It does not show how a terminal displays them.
- **The system backend's refused path.** With stdout redirected to a
  file, create returns `OVD_ERROR_NOT_A_TERMINAL` and writes nothing
  (`test_system_notty`, every platform above).
- **Wine is not Windows.** It runs the headless suite and the
  redirected-stdout path only. It is not evidence of behavior in Windows
  Terminal or the Windows console (`baseline.md`).
