# Manual validation checklist

Part 1, checks M1 to M10, is for `hello_overdrive` and feature 001's
SC-007. [Part 2](#part-2-input_demo), checks I1 to I12, is for
`input_demo` and feature 002's SC-006. Both parts use the build steps and
the "If a run leaves the terminal changed" section of part 1.

## Part 1: `hello_overdrive`

Part 1 is how SC-007 is met: `hello_overdrive` passes it in Windows
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
- The prompt follows below it. The summary's line break is on the last
  row, so the screen scrolls up one row and the frame's top row leaves
  the screen. That is expected.
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
ended the process in a way the library cannot catch (`kill -KILL` on
POSIX, or ending the task from Task Manager on Windows), or a check fails.
The library restores the terminal after the catchable signals, crashes,
and console events that feature 002 lists (spec FR-030); part 2 checks
those.

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

## Part 2: `input_demo`

Part 2 is how feature 002's SC-006 is met: `input_demo` passes it in
Windows Terminal and in one POSIX terminal, with versions recorded. It
checks User Stories 2 and 3 of `specs/002-input-restore/spec.md`:
- reading keys, mouse, paste, focus, and resizes from a real terminal;
- restoring the terminal when the process ends without destroying it.

CI covers the headless decoder and a pseudo-terminal test of the modes and
of each signal on POSIX. A pass here is evidence only for the terminal,
version, and operating system it records.

### Build

Build as in part 1, with the target `input_demo`:

```sh
cmake --preset debug
cmake --build --preset debug --target input_demo
build/debug/bin/input_demo
```

On Windows, run `build\debug\bin\input_demo.exe` from a Windows Terminal
tab, as in part 1. Below, `input_demo` stands for the program at its path.

Don't use a `sanitize` build for part 2. The sanitizer's own signal
handler runs when the program crashes, so `--crash=segv` then ends by
SIGABRT, and I10 doesn't show what it checks.

### What the program does

```text
input_demo [--mouse] [--paste] [--focus] [--crash=abort|segv]
input_demo --no-input
```

- **Screen.** The program takes the terminal, with the alternate screen and
  raw input, and shows:
  - a header with the options in effect and the size;
  - a **Last events** panel with the last 12 events, newest at the bottom
    in green;
  - a footer with the keys it acts on, and the number of events read.
- **Options.** `--mouse`, `--paste`, and `--focus` turn on mouse reports,
  bracketed paste, and focus reports. Each option may be given once.
- **Waiting.** The program waits for input without using the processor.
  With `--no-input` it sleeps and redraws about every 100 ms, so a resize
  still shows.
- **Quitting.** `q` quits. The program restores the terminal, then prints
  `input_demo: N events read`, and the exit status is 0.
- **Crash.** With `--crash=abort` or `--crash=segv`, pressing `c` ends the
  process without restoring the terminal itself:
  - `abort` calls `abort`;
  - `segv` raises SIGSEGV on POSIX and makes an access violation on
    Windows.
  The library's abnormal-exit restoration must then restore the terminal.
- **Input off.** `--no-input` leaves input off: the screen says so, and
  only Ctrl+C ends the program.

Event lines look like this:

| Event | Line |
|---|---|
| A character | `key   x  (U+0078)`: the character, then its code point |
| A character with modifiers | `key   Alt+a  (U+0061)`; a control character is its letter or symbol with `Ctrl+`: `key   Ctrl+c  (U+0063)` |
| A named key | `key   Ctrl+Up`, `key   Shift+Tab`, `key   F5` |
| Mouse | `mouse press left at 10,4`; the last cell is marked with an orange `+` |
| Paste | `paste 23 bytes, first line: hello` |
| Focus | `focus in`, `focus out` |
| Resize | `size  120x30` |

### Before the checks

Record the same facts as in part 1, "Before the checks". On Windows, also
note the output code page before the run, as in M2.

After every check, the shell must be as M2's "After it exits" describes:
- the screen as before the run, plus any message;
- the cursor visible;
- typing echoes;
- line editing works (the arrows and Backspace edit the command line);
- autowrap is on.

"The shell is restored" below means exactly that. On POSIX,
`stty -a` before and after the run shows the same settings.

### I1: Keys

Run `input_demo`. Press each key below, and check that each gives one
event line naming it:
- letters, digits, and punctuation; with Shift, the shifted character;
- a non-ASCII character, if the keyboard has one;
- the four arrows, Home, End, Page Up, Page Down, Insert, Delete;
- F1 to F12;
- Tab, Shift+Tab (`key   Shift+Tab`), Backspace, Enter, and Escape. Escape
  appears after a delay of about 50 ms;
- the arrows and Home and End with Shift, Alt, and Ctrl, each alone and
  together, for example `key   Ctrl+Up` or `key   Shift+Alt+Right`;
- Alt with a letter: `key   Alt+a  (U+0061)`.

**Expected:**
- Nothing is echoed, and no key waits for Enter.
- The terminal or the operating system may take some combinations for
  itself, such as Alt+F4, Ctrl+Tab, or F11 for full screen, so the program
  never sees them. Record which.
- As `input.h` says, a terminal cannot tell Ctrl+I from Tab, Ctrl+M from
  Enter, or Ctrl+[ from Escape; it may report Ctrl+H as Backspace. Those
  report the named key, and that is a pass.

Press `q`. **Expected:** the shell is restored, and the exit status is 0.

### I2: Ctrl+C, Ctrl+\, and Ctrl+Z are keys

Run `input_demo`, and press Ctrl+C, Ctrl+\, and Ctrl+Z.

**Expected:**
- They appear as `key   Ctrl+c  (U+0063)`, `key   Ctrl+\  (U+005C)`, and
  `key   Ctrl+z  (U+007A)`.
- The program keeps running. It is not interrupted, it does not quit, and
  it is not suspended.

Press `q`. **Expected:** the shell is restored.

### I3: Mouse

Run `input_demo --mouse`. Click, hold and drag, release, and turn the wheel
up and down. Use the left, middle, and right buttons, and click with Shift,
Alt, or Ctrl held.

**Expected:**
- **Presses and releases:** `mouse press` and `mouse release` with the
  button, at the cell under the pointer. Cells count from 0,0 at the top
  left. The orange `+` follows the last cell.
- **Drags:** one `mouse drag` line for each new cell while a button is
  held.
- **Wheel:** `mouse wheel up` and `mouse wheel down`, one line per step.
- **No hover:** moving without a button gives nothing.
- **Modifiers:** shown before the action, except where the terminal keeps
  the combination for itself; record which. Many terminals use Shift+drag
  for their own selection while the mouse is on.
- **After quitting:** clicking in the shell after `q` prints nothing
  strange, so mouse reporting is off again.

### I4: Text selection with the mouse off

Run `input_demo` without `--mouse`. Drag across the event lines with the
left button.

**Expected:**
- The terminal selects the text as usual, and the selection can be copied.
- No mouse event appears.

Press `q`. **Expected:** the shell is restored.

### I5: Paste

Copy a few lines of text, for example three lines of which one is longer
than 48 characters. The text must not contain the letters `q` or `c`: in
the second run below, they arrive as keys, and `q` would quit the program.
For example:

```text
A brown fox jumps over the lazy dog, then naps until noon.
Two lines more.
That is all.
```

Run `input_demo --paste`, and paste with the terminal's paste command.

**Expected:**
- One line, `paste N bytes, first line: ...`, where N is the size of the
  pasted text in bytes, line breaks included.
- The first line is shown, cut to at most 47 bytes.
- No key events are produced by the pasted text.

Then run `input_demo` without `--paste` and paste the same text. Expected:
it arrives as keys. A line break shows as `key   Enter`, or as
`key   Ctrl+j  (U+006A)` where the terminal pastes it as a line feed.

Press `q` each time. **Expected:** the shell is restored, and pasting in
the shell afterwards does not show `200~` or `201~`.

### I6: Focus

Run `input_demo --focus`. Switch to another window or tab, then back.

**Expected:** `focus out`, then `focus in`.

Press `q`. **Expected:** the shell is restored, and switching windows
afterwards prints nothing in the shell.

### I7: Resize while idle

Run `input_demo`. Without pressing a key, make the window smaller, then
larger. On Windows, also maximize it and restore it.

**Expected:**
- A `size  CxR` line appears for each new size, and the header's size
  matches the window.
- The screen redraws at once, filling the window with no leftover text.

Press `q`. **Expected:** the shell is restored.

### I8: Closing the window

Run `input_demo` in a window or tab of its own, then close that window or
tab with the terminal's close button.

**Expected:**
- **The process ends.** Check from another shell that no `input_demo`
  process is left: `ps aux | grep input_demo` on POSIX, Task Manager on
  Windows.
- **Windows:** the console sends `CTRL_CLOSE_EVENT`. The library restores
  the terminal, then the process ends.
- **POSIX:** closing the window sends SIGHUP. The library restores what it
  can; the terminal is already going away.

There is no screen to look at afterwards; the check is that the process
ended and the terminal program reported no error.

### I9: Crash with `abort`

Run `input_demo --crash=abort`, press a few keys, then `c`.

**Expected:**
- The shell is restored.
- The shell reports the abort:
  - POSIX: `Abort trap: 6` or `Aborted`, and `echo $?` prints 134;
  - Windows: `$LASTEXITCODE` is 3.
- Windows may show an error-reporting dialog first; close it, and record
  that it appeared.

### I10: Crash with `segv`

Run `input_demo --crash=segv`, then press `c`.

**Expected:**
- The shell is restored.
- The shell reports the crash:
  - POSIX: `Segmentation fault`, and `echo $?` prints 139;
  - Windows: `$LASTEXITCODE` is -1073741819 (0xC0000005, an access
    violation).
- Windows may show an error-reporting dialog first, as in I9.

### I11: `kill -TERM` (POSIX)

Run `input_demo`. From another shell, run `pkill -TERM -x input_demo`.

**Expected:**
- The program ends at once.
- The shell is restored.
- The shell reports `Terminated`, and `echo $?` prints 143.

### I12: Ctrl+C with input off (Windows)

Run `input_demo --no-input`. The panel reads **Input is off**. Press
Ctrl+C.

**Expected:**
- The terminal is restored first, then the process ends.
- The tab stays open, and the shell is restored.
- `$LASTEXITCODE` is -1073741510 (0xC000013A, ended by Ctrl+C). This
  value is the console's default handling, which the library passes the
  event on to; it has not been observed yet, so record what you see.
- The code page is as before the run.

On POSIX, the same run ends by SIGINT, with the shell restored and
`echo $?` printing 130. Run it if you like, and record the result in the
notes.

### Results template (part 2)

Copy this into the pull request or issue comment, once per terminal.
Results are `pass`, `fail` (with what was seen), or `not run`.

```markdown
### input_demo manual checklist

- Commit:
- Built with: (preset, compiler and version)
- Terminal: (name, version)
- Operating system: (name, version)
- Shell:
- TERM / COLORTERM: (POSIX only)

| Check | Result | Notes |
|---|---|---|
| I1 Keys | | |
| I2 Ctrl+C, Ctrl+\, and Ctrl+Z are keys | | |
| I3 Mouse | | |
| I4 Text selection with the mouse off | | |
| I5 Paste | | |
| I6 Focus | | |
| I7 Resize while idle | | |
| I8 Closing the window | | |
| I9 Crash with abort | | |
| I10 Crash with segv | | |
| I11 kill -TERM (POSIX) | | |
| I12 Ctrl+C with input off (Windows) | | |
```
