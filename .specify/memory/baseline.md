# Machine Baseline

Referenced by the constitution, Article VIII. It says what may be assumed
on a contributor's development machine. Normative, but amendable without
constitutional process. The binding rules live in Article VIII: nothing in
a declared build, test, or CI path assumes anything listed here without
declaring a minimum version, and AI or dev-assist tooling never enters
the library's build or test path.

## Build environment

Overdrive is a portable library: it must build warning-free under both
supported compiler families on every supported platform.

| Tool | Minimum | Check |
|---|---|---|
| GCC | 14 | `gcc-14 --version` |
| Clang (LLVM) | 19 | `clang --version` |
| Apple Clang | 17 | `cc --version` |
| CMake | 3.28 | `cmake --version` |
| Ninja | 1.11 | `ninja --version` |
| Git | 2.43 | `git --version` |

C23 features the public headers rely on — `bool` and `nullptr` as
keywords, `[[nodiscard]]`, enums with a fixed underlying type — are the
reason for the compiler minimums, together with object `constexpr`,
which Article III asks for in named constants. Clang 19 is the first
Clang with object `constexpr` (Clang's C status page, N3018), so it is
the Clang minimum; Clang 18 builds the headers but not that. The Apple
Clang minimum is provisional: it is confirmed, or raised, by building
the headers and an object `constexpr` under `-std=c23 -Wpedantic
-Werror` on that version, and this table is amended with the result.
MSVC is not supported: its C23 support does not cover them.

CI builds with the GCC and Clang minimums. It does not prove the CMake
or Apple Clang minimums: it uses the CMake that the runner image
provides, which can be newer than the minimum, and `macos-latest` moves
with GitHub's image. Each CI job logs the tool versions it used. A
change that relies on one of those minimums is checked on that version
by hand.

Platform packages:

- **Ubuntu 24.04:** `sudo apt install -y gcc-14 clang-19 cmake ninja-build git`
- **macOS (Homebrew):** Xcode Command Line Tools, then
  `brew install gcc@14 cmake ninja`
- **Windows, native:** MSYS2 UCRT64 —
  `mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`

Select a compiler with `CC` or a git-ignored `CMakeUserPresets.json`; the
shared presets leave discovery to CMake.

## Sanitizers (the `sanitize` preset)

The `sanitize` preset builds with AddressSanitizer and
UndefinedBehaviorSanitizer and stops at the first report. Its runtimes
come with the compiler, so it adds no separate tool, but not every
compiler on the list above ships them for every platform:

| Platform | Compiler | Runtime |
|---|---|---|
| Linux | GCC 14 | `libasan` and `libubsan`, installed with the `gcc-14` package on Ubuntu 24.04 |
| macOS | Apple Clang | the compiler's own sanitizer runtime (Xcode Command Line Tools) |

CI runs the preset on exactly these two. Homebrew GCC on macOS is not
used for it. Windows (MinGW-w64 under Wine) is not part of the sanitizer
path.

The sanitize test preset leaves out `check_exports` on the library.
AddressSanitizer adds its own global symbols to the instrumented archive
(`asan_globals_registered`, for example), which that check would report
as non-`ovd_` exports. Every other preset runs it.

## Windows cross-build (headless tests under Wine)

| Tool | Minimum | Debian/Ubuntu package | Homebrew | Check |
|---|---|---|---|---|
| MinGW-w64 GCC | 14 | `gcc-mingw-w64-x86-64` (Debian trixie) | `mingw-w64` | `x86_64-w64-mingw32-gcc --version` |
| Wine | 10.0 | `wine`, `wine64` | `wine-stable` (cask) | `wine --version` |

Wine runs the headless test suite only. It is not evidence of behavior in
Windows Terminal or the Windows console; that is checked by hand on
Windows and reported with the terminal's version.

## Manual validation terminals

Not part of any declared path; listed so reports name them consistently.

- **Windows Terminal** (primary validation target), on Windows 10 1809+ or
  Windows 11.
- One POSIX terminal: macOS Terminal, iTerm2, or a Linux terminal
  emulator, reported with name and version.

## Agent and convenience tooling (never in the build path)

- **gh** — GitHub CLI, for issue and PR reads and human-approved writes.
- **jq** — required by `.claude/hooks/guard.sh`.
- **just** — optional wrapper around the presets; never adds steps CI
  lacks.
- **clangd** — language server for impact analysis (Art. I).
- AI coding tools (Claude Code and others).

If CI needs it, declare it; if only the developer needs it, keep it out of
the build path.

## Change Log
- 2026-10-26 — Initial baseline, adapted from the C23 port's baseline:
  Clang added as a supported compiler family, MSVC excluded, manual
  validation terminals listed.
- 2026-11-05 — Sanitizers section: the `sanitize` preset's runtimes per
  platform (GCC 14 on Linux, Apple Clang on macOS).
