# Manual validation checklist: `hello_overdrive`

This checklist is how SC-007 is met: `hello_overdrive` passes it in Windows
Terminal and in one POSIX terminal, with versions recorded. It checks User
Story 2 ("Acquire and restore a real terminal") by hand. A person runs it,
because only a real terminal shows what a person sees. It also checks the
terminal behaviors that earlier decisions could only take from
documentation: the "Unverified" list in research §E, and R33, R34, R38, and
R40.

CI already covers two things automatically: the headless test suite, and
`hello_overdrive_notty`, which runs the example with stdout redirected to a
file. A pass here is evidence only for the terminal, version, and operating
system it records. Record results with the template at the end, in the pull
request or issue that asked for the run. Do not record them in this file.

## Build

The example is built with the shared presets, which turn
`OVERDRIVE_BUILD_EXAMPLES` on.

POSIX (macOS or Linux), from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug --target hello_overdrive
build/debug/bin/hello_overdrive
```

Windows has two ways to build. Either way, run the program from a Windows
Terminal tab with a PowerShell or Command Prompt profile. SC-007 names
Windows Terminal, so other Windows terminals, such as the MSYS2 window, do
not count for this checklist.

- **Native.** In an MSYS2 UCRT64 shell (see the machine baseline), build
  with the same two `cmake` commands as above. If Windows reports a missing DLL,
  add the UCRT64 `bin` directory to `PATH` in the tab that runs the program.
  Then run the program from Windows Terminal:

  ```bat
  build\debug\bin\hello_overdrive.exe
  ```

- **Cross-built.** On macOS or Linux, build it and copy the program to the
  Windows machine:

  ```sh
  cmake --preset windows-cross-debug
  cmake --build --preset windows-cross-debug --target hello_overdrive
  ```

  The program is `hello_overdrive.exe`, in the `bin` directory of that
  preset's build directory.

In the checks below, `hello_overdrive` stands for the program at its path
from the build: `hello_overdrive.exe` on Windows.

## What the program does

```text
hello_overdrive [--synchronized-output] [--no-alternate-screen] [seconds]
```

The program runs for 5 seconds by default, or for 1 to 60 seconds if you
give a number. It shows 20 frames per second. It keeps one processor core
busy while it runs, because portable C has no sleep, and that is expected.
Ctrl+C ends a run early.

By default it uses what a consumer gets from the library's defaults: the
alternate screen, without synchronized output. The two options change those
settings for M8 and M9.

When it ends, it restores the terminal and then prints one line. A normal
run prints, for example:

```text
hello_overdrive: 100 frames at 120x30, truecolor
```

A run stopped with Ctrl+C prints the same line with `, stopped by Ctrl+C` at
the end. On an error it prints the failing call and result to stderr, and
the exit status is 1.

## Before the checks

Record the facts the results template asks for:
- the commit;
- how the program was built;
- the terminal's name and version;
- the operating system and its version;
- the shell;
- on POSIX, `TERM` and `COLORTERM`.

To find them:
- **Windows Terminal:** About, in the dropdown menu next to the tabs.
- **Windows:** `winver`.
- **macOS:** `sw_vers`.
- **Linux:** `uname -a`, plus the terminal's own About dialog or `--version`.

The color depth the program reports follows the library's detection, first
match wins:

| Condition | Reported |
|---|---|
| `NO_COLOR` is set and not empty | `none (attributes only)` |
| `COLORTERM` is `truecolor` or `24bit` | `truecolor` |
| Windows (creating the terminal always turns on virtual-terminal output) | `truecolor` |
| `TERM` contains `256color` | `256 colors` |
| `TERM` is `dumb` | `none (attributes only)` |
| anything else | `16 colors` |

Below truecolor, the swatches show the nearest color the terminal has.

## Checks

Run M1 first; each later check compares the terminal with how M1 left it.

### M1: Screen before the run

Fill the window with recognizable output, so the visible screen and the
scrollback both hold text, for example by listing a directory a few times.
Note the last visible lines, and where the prompt is.

### M2: A normal run

On Windows, first run this, and note the number it prints. It is the
console's output code page, which the program changes while it runs:

```bat
powershell -NoProfile -Command "[Console]::OutputEncoding.CodePage"
```

Then run `hello_overdrive` with no argument.

**During the run:**
- The whole window is Carbon Black (`#0B1018`), up to the last column and
  the last row.
- At the top left of a centered block, "Overdrive" appears in bold Ignition
  Orange, with "Every frame matters." under it.
- A **Palette** panel with a rounded border lists the seven swatches with
  their hex values: carbon, graphite, ignition, electric, signal,
  foreground, muted.
- A **Frame** panel with a heavy orange border shows:
  - `frame n of 100`, counting up;
  - a green bar filling from left to right;
  - `size` equal to the window's columns and rows;
  - `colors` as the table above predicts;
  - `screen alternate` and `sync off`;
  - `library Overdrive` followed by the version.
- Five boxes, labelled `light`, `heavy`, `double`, `rounded`, and `ascii`,
  each drawn in its own border style. The ASCII box uses `+`, `-`, and `|`.
- A `zwj check` row (see M10).
- The footer reads `Runs for 5 s; Ctrl+C stops it early.`
- Only the counter and the bar change between frames. The screen never
  scrolls, and the cursor is hidden.
- Keys typed during the run are not shown. The shell may receive them
  after the program exits.

**After it exits:**
- The screen is as M1 left it, plus the summary line and a new prompt.
- The scrollback holds what it held before, and nothing from the run.
- The cursor is visible, and the prompt has its usual colors.
- Typing echoes again.
- Autowrap is on: a line longer than the window wraps onto the next line.
  - POSIX: `printf '%0300d\n' 0`
  - PowerShell: `'x' * 300`
- On Windows, the code-page command prints the same number as before the
  run. If it printed 65001 before the run, this step cannot show a
  restore, because 65001 is the code page the program sets; note that in
  the results.

### M3: Resize during a run

Run `hello_overdrive 30`. While it runs, drag a window edge to make the
window smaller, then larger. Also go below 74 columns and below 20 rows.
On Windows, also maximize the window and restore it.

**Expected:**
- Within a moment of each resize, `size` shows the new size.
- The block re-centers. When the window is smaller than the block, it is
  cut off at the right and the bottom instead.
- Carbon Black fills the window exactly, up to the new last column and
  last row.
- Nothing is left over from the old size, and no line wraps.

Then let the run end, or press Ctrl+C. The terminal is restored as in M2.

### M4: Ctrl+C

Run `hello_overdrive 30`, and press Ctrl+C after a few seconds.

**Expected:**
- The run stops at once.
- The terminal is restored exactly as in M2's "After it exits".
- The summary line ends with `, stopped by Ctrl+C`.
- The exit status is 0. Check it with `echo $?` (POSIX), `$LASTEXITCODE`
  (PowerShell), or `echo %errorlevel%` (Command Prompt).

### M5: Stdout redirected to a file

This is User Story 2, scenario 3. On Windows, run it in a Command Prompt
tab, where `>` gives the program a file:

```bat
hello_overdrive.exe > out.txt
echo %errorlevel%
dir out.txt
```

POSIX:

```sh
build/debug/bin/hello_overdrive > out.txt; echo $?; wc -c < out.txt
```

**Expected:**
- `hello_overdrive: ovd_terminal_create failed: OVD_ERROR_NOT_A_TERMINAL`
  appears on the screen.
- The status is 1.
- `out.txt` has 0 bytes.
- The screen is unchanged: no alternate screen, the cursor is visible, and
  typing echoes.

On Windows this tests, on a real console, research R40's reading of
Microsoft's documentation: that `GetConsoleMode` fails for a redirected
stdout.

### M6: Started in the background (POSIX only)

Run `hello_overdrive 5 &`.

**Expected:**
- The shell reports the job as stopped at once, for terminal output.
  Creating the terminal changes the input modes, and a background process
  that changes its terminal's modes is stopped by SIGTTOU. Overdrive does
  not block that signal, because doing so would change process-wide state.
- The screen is unchanged.

Then run `fg`.

**Expected:** the program runs and restores the terminal as in M2.

### M7: The frame fills the window exactly

During M2 and M3, look at the right edge and the bottom edge in particular.
Check them at the starting size and at a resized size, and on Windows also
maximized.

**Expected:**
- The last column and the last row are Carbon Black.
- No column or row is missing, and none wraps onto the next line.
- The window does not scroll.

The renderer writes the last column without relying on the terminal's
wrapping (research R33). On Windows this also tests research R40's reading
that the corners of the console window rectangle are inclusive.

### M8: Without the alternate screen

Run `hello_overdrive --no-alternate-screen 5`.

**During the run:**
- The frame replaces what was on the screen, and the Frame panel shows
  `screen main`.
- Otherwise the run looks as in M2.

**After it exits:**
- The last frame stays on the screen.
- The summary line starts at the left end of the frame's last row. The
  library leaves the cursor at column 0 of that row.
- The prompt follows below it.
- The cursor is visible, typing echoes, and autowrap is on, checked as in
  M2.

Whether the earlier screen contents reach the scrollback depends on the
terminal; note what you see.

### M9: Synchronized output (mode 2026)

Run `hello_overdrive --synchronized-output 5`. Each frame is then wrapped in
the mode 2026 begin and end markers.

**Expected:**
- The run looks exactly as in M2, except that the Frame panel shows
  `sync mode 2026`.
- No stray characters appear anywhere, such as `[?2026h`. A terminal
  without mode 2026 must ignore the markers (research R34).
- The terminal is restored as in M2.

Note in the results whether the terminal's documentation says it supports
mode 2026.

### M10: A ZWJ cell followed by a cursor move

The `zwj check` row draws two cells side by side:
- WOMAN followed by ZERO WIDTH JOINER, as one cell's text;
- GIRL, drawn separately in the next cell.

The renderer moves the cursor between the two. Research R38 assumes that
this stops a terminal from joining them into one glyph.

**Expected:**
- Two separate figures, a woman and then a girl, each two columns wide.
- A single combined figure means that this terminal joins across a cursor
  move: R38's assumption does not hold there. Record it as a failure.
- If the font has no emoji, two placeholder glyphs count as separate;
  record that the font lacks them.

## If a run leaves the terminal changed

The terminal stays as the program changed it in two cases: something
killed the process before it restored the terminal (a signal other than
SIGINT, or a closed window), or a check fails.

On POSIX, type `reset` and press Enter. You will not see the letters as you
type them. On Windows, close the tab and open a new one.

Record the failure, and what was on the screen, in the results.

## Results template

Copy this into the pull request or issue comment, once per terminal.
Results are `pass`, `fail` (with what was seen), or `not run`.

```markdown
### hello_overdrive manual checklist

- Commit:
- Built with: (preset, compiler and version)
- Terminal: (name, version)
- Operating system: (name, version)
- Shell:
- TERM / COLORTERM: (POSIX only)

| Check | Result | Notes |
|---|---|---|
| M1 Screen before the run | | |
| M2 A normal run | | |
| M3 Resize during a run | | |
| M4 Ctrl+C | | |
| M5 Stdout redirected to a file | | |
| M6 Started in the background (POSIX) | | |
| M7 The frame fills the window exactly | | |
| M8 Without the alternate screen | | |
| M9 Synchronized output (mode 2026) | | |
| M10 A ZWJ cell followed by a cursor move | | |
```
