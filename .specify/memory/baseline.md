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
| Clang (LLVM) | 18 | `clang --version` |
| Apple Clang | 17 | `cc --version` |
| CMake | 3.28 | `cmake --version` |
| Ninja | 1.11 | `ninja --version` |
| Git | 2.43 | `git --version` |

C23 features the public headers rely on — `bool` and `nullptr` as
keywords, `[[nodiscard]]`, enums with a fixed underlying type — are the
reason for the compiler minimums. The Clang minimums are provisional:
they are confirmed, or raised, by building the headers under
`-std=c23 -Wpedantic -Werror` on that version, and this table is amended
with the result. MSVC is not supported: its C23 support
does not cover them.

Platform packages:

- **Ubuntu 24.04:** `sudo apt install -y gcc-14 clang-18 cmake ninja-build git`
- **macOS (Homebrew):** Xcode Command Line Tools, then
  `brew install gcc@14 cmake ninja`
- **Windows, native:** MSYS2 UCRT64 —
  `mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`

Select a compiler with `CC` or a git-ignored `CMakeUserPresets.json`; the
shared presets leave discovery to CMake.

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
