## Context

See proposal.md for the motivation. The example is the first program
that uses the system backend the way a consumer would. It goes through
the public API only (`<overdrive/overdrive.h>`). Three behaviors of
that API shape the design:
- `ovd_terminal_create` turns off input echo only. POSIX keeps `ISIG`,
  and Windows keeps `ENABLE_PROCESSED_INPUT`, so Ctrl+C still raises
  SIGINT while the example runs.
- Each `ovd_frame_begin` re-reads the size (FR-014), so a resize shows
  up in the next frame with no extra code.
- `ovd_terminal_destroy` restores the terminal, but it is not
  async-signal-safe. If the process dies without calling it, the
  terminal stays changed.

## Goals / Non-Goals

**Goals:**
- One portable ISO C program that builds with every shared preset,
  with no operating-system header. The `AGENTS.md` layout gives
  `src/platform/` as the only place that includes one.
- Every SC-007 check can be observed on the example's screen. A
  resize changes the displayed size and re-centers the layout, and
  Ctrl+C ends the run with the terminal restored.
- Every `OvdResult` is checked, and a failure never leaves the
  terminal changed.
- A checklist a person can follow without reading the code. Each step
  has an expected result, and the results template records versions.

**Non-Goals:**
- Reading input, beyond Ctrl+C arriving as a signal. The library does
  not read input in this version.
- Running the checklist. The issue's human follow-up does that, and it
  does not block the merge.
- Driving a pseudo-terminal in a regression test. That is #43.
- Any claim about speed or smoothness (Art. XV, `OVD-IDENTITY-001`
  §7.2).

## Decisions

- **Frames are paced by spinning on a clock.** Frame *n* is shown no
  earlier than *n* × 50 ms after the first, giving 20 frames per second.
  - Where `<time.h>` has `timespec_get`, the clock is `TIME_MONOTONIC`
    when `<time.h>` defines it (optional in C23) and it works, else
    `TIME_UTC`. That is the same rule as the benchmark (R47).
  - mingw-w64 declares `timespec_get` and `TIME_UTC` only for the UCRT
    runtime, and the CI cross toolchain lacks both, as CI's first build
    of this change showed. There the clock is `clock()`. Microsoft's C
    runtime documents it as wall time since the process started.

  A clock that fails, or that jumps back past the start, ends the wait
  instead of hanging.
  *Rejected:*
  - `thrd_sleep`: `<threads.h>` is absent from both the macOS SDK
    with AppleClang 21 and MinGW-w64 GCC 15.2. Both were observed.
  - `nanosleep` and `Sleep`: they need OS headers outside
    `src/platform/`.
  *Cost:* one core stays busy for the run's few seconds.
- **The run lasts 5 seconds by default (100 frames).** A number
  argument, if given, sets the duration: 1 to 60 seconds, as decimal
  digits only. Any other argument, except the two options below, prints
  usage and exits 1. The checklist
  uses a longer run for the resize and Ctrl+C steps, because 5 seconds
  is too short to drag a window edge.
- **Ctrl+C sets a flag.** It is installed with ISO `signal(SIGINT, …)`.
  The handler writes a lock-free `atomic_bool` and installs itself
  again, which ISO C allows. A `volatile sig_atomic_t` is enough only
  when the handler interrupts the thread that reads the flag. The
  Windows C runtime runs the handler on a new thread, and an atomic is
  defined there too. Both the System V semantics that strict
  glibc gives and the Windows C runtime reset the handler before
  calling it, so without the re-install a second Ctrl+C would kill the
  process before the terminal is restored. The frame loop and the
  wait both check the flag, so the run ends within one frame and then
  takes the normal exit path. If `signal` fails, the example says so
  and exits 1 before create, so nothing has changed.
- **The default config is used unless an option says otherwise.** The
  defaults are the system backend, color depth detected from the
  environment, the alternate screen on, and synchronized output off, so
  the main checks test what a consumer gets.

  Two options, each allowed once, in any order with the duration, change
  one field each:
  - `--synchronized-output` sets `synchronized_output`;
  - `--no-alternate-screen` clears `alternate_screen`.

  They exist because earlier decisions deferred real-terminal checks to
  this checklist, and the defaults never exercise them:
  - research §E: "terminals ignoring mode 2026 when unsupported" (R34);
  - the exit sequence's CUP, used only without the alternate screen
    (R26, R46). The comment in `src/terminal_system.c` defers it.

  The Frame panel shows both settings.
  *Rejected:* environment variables, which a consumer's config would
  never read.
- **One row shows R38's case: a cell whose grapheme ends in ZWJ,
  followed by a cursor move.** The row draws U+1F469 followed by
  U+200D in one call. It then draws U+1F467 in the next cell with a
  separate call. The renderer always moves the cursor between the two,
  and R38 assumes that the move stops a terminal from joining them. The
  row never changes, so it is written only in full redraws.
- **The layout is a fixed 74×20 block, centered and recomputed every
  frame from the canvas size.** A resize then shows on screen, because
  the block re-centers and the size readout changes. A smaller terminal
  clips the block, which the canvas does without failing. The block
  contains:
  - the name in Ignition Orange, bold, with the tagline under it;
  - a palette panel listing the seven §6.1 tokens;
  - a heavy-bordered "Frame" panel, the one orange accent, showing:
    - the counter, "frame *n* of *total*";
    - a progress bar in Signal Green;
    - the size;
    - the color depth;
    - the screen and synchronization settings;
    - the library version from `ovd_version()`;
  - five boxes labelled with the five `OvdBorder` styles;
  - the ZWJ row;
  - a footer that says how the run ends.

  Colors follow §6.1:
  - the page is Carbon Black;
  - panels are Graphite;
  - labels are Muted Steel;
  - values are High-contrast White.

  Swatches and the bar are filled cells, not block glyphs, so they do
  not depend on the font (§6.3). Each swatch shows its hex value in a
  contrasting color, so the Carbon and Graphite swatches stay visible
  on their panel.
- **Failures are reported after restore.** On any failure the loop
  stops, the terminal is destroyed, and only then does the error go to
  stderr, naming the call and `ovd_result_name`. The message therefore
  lands on the restored screen. A normal run prints its summary to
  stdout after destroy: frames shown, last size, color depth, and
  whether Ctrl+C ended it. Exit status is 0 on success and 1 otherwise.
- **Examples are on in the base preset.** `CMakePresets.json` sets
  `OVERDRIVE_BUILD_EXAMPLES` to true in the hidden base preset, so
  every shared preset, and CI's debug, release, shared, Clang, and
  MinGW builds, compile the example with warnings as errors. The
  option's default in `CMakeLists.txt` stays off for consumers.
  *Rejected:* turning it on in `debug` alone. That would also make the
  proof command pass, but the release build would then never compile
  the example, and some GCC warnings appear only with optimization on.
- **Scenario 3 is automated for the example.** `hello_overdrive_notty`
  follows the driver pattern of `test_system_notty`: a generated CMake
  script runs the program with stdout sent to a file, through the
  cross emulator when there is one. It fails unless all of these hold:
  - the exit status is 1;
  - the file exists and is empty;
  - stderr contains `ovd_terminal_create failed:
    OVD_ERROR_NOT_A_TERMINAL`.

  It is registered only when examples are built.
- **The checklist holds steps, not results.** Results go in the PR,
  or in an issue comment for a later run, using the template at the
  end of the checklist. Each run is evidence for one terminal, version,
  and OS only. The checklist covers:
  - US2 scenarios 1, 2, and 4;
  - scenario 3 in Windows Terminal, run from Command Prompt so that
    `>` gives the program a file handle;
  - the three checks from the issue's comment: the background start on
    POSIX, the redirected stdout on Windows, and on Windows a frame that
    fills the window exactly. The last one is checked on both platforms,
    because R33's last-column handling is deferred too;
  - Ctrl+C;
  - the deferred checks:
    - mode 1049 restoring the screen;
    - the Windows output code page restored, read from a new PowerShell
      process;
    - the exit sequence without the alternate screen;
    - mode 2026 ignored or honored without stray output;
    - R38's ZWJ row;
  - recovery commands, in case a run leaves the terminal changed.

  Commands appear only in code blocks, because the docs-form check
  rejects backtick paths that are gitignored, such as build outputs.

## Risks / Trade-offs

- [Windows runs the SIGINT handler on a new thread, per Microsoft's
  `signal` reference] → The handler only writes the flag, a lock-free
  atomic object, so the frame loop's reads do not race with it. Whether
  Ctrl+C restores the terminal on Windows is ASSUMED until the
  checklist's Ctrl+C step runs there.
- [`TIME_UTC` is a wall clock and can jump] → A backward jump past the
  start ends the wait. A forward jump only shortens the run.
- [The `clock()` fallback is wall time only by Microsoft's definition;
  ISO C defines it as processor time] → The fallback is compiled only
  where `<time.h>` lacks `TIME_UTC`, which is MinGW-w64 without UCRT.
  Elsewhere it would still pace about right, because the spinning keeps
  processor time close to wall time. A scratch build that forced the
  fallback on macOS ran 40 frames in a 2-second run.
- [Spinning keeps one core busy] → The run is short and the design
  says so. Pacing that sleeps needs an OS header or `<threads.h>`.
- [A signal other than SIGINT, such as SIGTERM, SIGHUP, or a closed
  window, still kills the process with the terminal changed] → This is
  the documented `ovd_terminal_destroy` limit. The checklist's
  recovery section covers it.
- [The ZWJ row needs an emoji font to show figures] → Placeholder
  glyphs still show whether the two cells stay separate, and the
  checklist says to record a missing font.
- [CI cannot see the rendered screen] → CI checks compilation and
  scenario 3 only. During development the screen is checked by running
  the example in a scratch pseudo-terminal and replaying its output
  through the test replay interpreter. The real-terminal checks stay
  UNVERIFIED until a person runs the checklist.
