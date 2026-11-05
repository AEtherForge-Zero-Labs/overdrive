## Context

The existing example, `hello_overdrive`, shows rendering and restoring the terminal. It also has a manual checklist, M1 to M10. Feature 002 adds input (`input.h`) and abnormal-exit restoration (`restore_on_exit`, on by default). SC-006 asks for an example and checklist steps that a person runs in real terminals.

## Decisions

### D1. Event-driven, no spinning

The demo waits with `ovd_input_wait(terminal, OVD_WAIT_FOREVER, &event)` and draws a frame after each event. A resize arrives as an event, so the frame follows the window, and the program uses no processor while idle. `hello_overdrive` spins only because 001 had no wait.

### D2. What is shown

- A header shows the program name and the options in effect.
- A log holds the last 12 events, newest at the bottom, one line each. Entries:
  - a key as its name with `Shift+`, `Alt+`, and `Ctrl+` prefixes. A character key shows its text when printable and `U+XXXX` when not, so Ctrl+C reads `Ctrl+c`;
  - a mouse event as its action, button, cell, and modifiers;
  - a paste as `paste: N bytes`, then its first line. The line is cut at a UTF-8 boundary and drawn through the canvas, so its controls are replaced;
  - focus in and out;
  - a resize as the new size.
- The last mouse cell is drawn in reverse video.
- A footer lists the keys the demo acts on.

### D3. Options

`input_demo [--mouse] [--paste] [--focus] [--crash=abort|segv] [--no-input]`.

Each option is allowed at most once.

- `--mouse`, `--paste` and `--focus` turn on the matching config field.
- `--no-input` turns input off. It cannot be combined with the three report options, or with `--crash`, which needs a key.

### D4. Crash on a key, not at start

With `--crash`, the demo crashes when `c` is pressed. The person sees the screen with every mode changed before the crash, which is what the restore must undo.

- `abort` calls `abort()`.
- `segv` on POSIX is `raise(SIGSEGV)`.
- `segv` on Windows writes through a volatile null pointer, giving the access violation that T121 names. The volatile pointer keeps the compiler from seeing a null dereference.

This is the narrowest reading of T121, which names the modes and not the moment.

### D5. Input off

`--no-input` is for SC-006's Windows check of Ctrl+C with input off: the terminal is restored, then the process ends. The demo installs no `SIGINT` handler, so the library's console handler restores and the default action ends the process.

It draws a static screen, then redraws it about every 100 ms, so a resize still shows. With no input, there is no wait to block in, so it paces itself with a clock. It uses `hello_overdrive`'s portable clock fallback, because MinGW's msvcrt has no `timespec_get`.

### D6. Checklist

Part 2 of `docs/validation/manual-checklist.md` gets these checks:

| Check | What it covers |
|---|---|
| I1 | Keys |
| I2 | Ctrl+C, Ctrl+\ and Ctrl+Z as keys |
| I3 | Mouse |
| I4 | Selection with mouse off |
| I5 | Paste |
| I6 | Focus |
| I7 | Resize while idle |
| I8 | Closing the window |
| I9 | `--crash=abort` |
| I10 | `--crash=segv` |
| I11 | `kill -TERM` (POSIX) |
| I12 | Ctrl+C with input off (Windows) |

Each check states the shell's state afterwards, and the part has its own results template. Running the checklist is the owner's follow-up.

## Risks / Trade-offs

- The demo is checked only by building it, which is the issue's proof, and by a pty smoke run here. Its behavior in real terminals is what the checklist checks.
