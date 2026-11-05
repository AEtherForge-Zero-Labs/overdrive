# Feature Specification: Input and Abnormal-Exit Restoration

**Feature Branch**: `002-input-restore`

**Created**: 2026-11-05

**Status**: Draft

**Input**: "Feature 002: terminal input and abnormal-exit restoration for
liboverdrive. Input: keys, mouse, paste, focus events, and raw input
mode. Abnormal-exit restoration: signals, Ctrl+C, crashes — the terminal
must be restored even when the consumer does not reach
ovd_terminal_destroy. Deferred from 001-render-core (see its Out of
scope section)."

## Scope in one paragraph

An application that renders with Overdrive can also read what the user
does in that terminal (keys, mouse, pasted text, focus changes, and
resizes) as typed events, waiting for the next one with a timeout. The
terminal is restored when the process ends without reaching
`ovd_terminal_destroy`: on an interrupt (Ctrl+C while input is off; with
input on it is a key), a termination request, or a crash.
This is the Input module of `OVD-IDENTITY-001` §4.7 and an extension of
the Platform module, at the smallest size the first consumer needs.
Feature 001's rendering contract does not change.

## User Scenarios & Testing *(mandatory)*

The users are developers of C applications that link Overdrive, and the
people who run those applications in a terminal.

### User Story 1 — Decode input headlessly (Priority: P1)

A developer creates a headless terminal with input enabled, gives it
bytes as a terminal would send them, and reads back events: a key with
its modifiers, a mouse action at a cell, a paste, a focus change.

**Why this priority**: The decoder is the whole input mechanism and the
untrusted boundary (Art. XIII). It can be proven with no terminal, so it
comes first (Art. V). Every other story uses it.

**Independent Test**: A test program feeds byte sequences from a table
written as readable text (Art. VI) and compares the decoded events with
the expected ones.

**Acceptance Scenarios**:

1. **Given** a headless terminal with input enabled, **When** the bytes
   `ESC [ A` are supplied, **Then** the next event is the Up key with no
   modifiers.
2. **Given** the same, **When** `ESC [ 1 ; 5 C` is supplied, **Then**
   the next event is Right with Ctrl.
3. **Given** the same, **When** the UTF-8 bytes of "é" are supplied,
   **Then** the next event is a text key for U+00E9.
4. **Given** the same, **When** a lone `ESC` is supplied and no further
   byte arrives within the escape delay, **Then** the next event is the
   Escape key.
5. **Given** the same, **When** an unrecognized escape sequence is
   supplied, **Then** it produces no key event and does not delay or
   corrupt the events after it.
6. **Given** the same, **When** no bytes are available and the wait's
   timeout elapses, **Then** the wait reports that no event arrived.

---

### User Story 2 — Read keys from a real terminal (Priority: P1)

A developer creates a system terminal with input enabled. Keys arrive
one at a time, unechoed, without waiting for Enter, while frames render.
The program can also wait for the next event without spinning. After
destroy, the shell's line editing and echo work as before.

**Why this priority**: It is the first consumer's need (001 research,
§B). Without it the consumer cannot drop its own input layer.

**Independent Test**: An example program, run by hand in Windows
Terminal and in one POSIX terminal, following a written checklist; plus
a pseudo-terminal test on POSIX that checks the modes before create,
during the run, and after destroy.

**Acceptance Scenarios**:

1. **Given** a system terminal with input enabled, **When** the user
   presses `q`, **Then** the program receives a text key for `q` before
   any Enter, and nothing is echoed.
2. **Given** the same, **When** the user presses an arrow, Home, End,
   Page Up/Down, Insert, Delete, F1–F12, Tab, Shift+Tab, Backspace,
   Enter, or Escape, **Then** the program receives that key.
3. **Given** the same, **When** the user resizes the window while the
   program waits, **Then** the wait returns a resize event with the new
   size.
4. **Given** the program exits normally through `ovd_terminal_destroy`,
   **Then** every input mode is restored to its value before create.
5. **Given** a system terminal created with input disabled (the
   default), **Then** its input modes and rendering are exactly feature
   001's.

---

### User Story 3 — The terminal survives an abnormal exit (Priority: P1)

The person running the program closes the window or sends a termination
request (including an interrupt sent with `kill`), or the program
crashes. Their terminal is back in its normal state: the main screen, a
visible cursor, echo and line editing on, mouse reporting off.

**Why this priority**: Raw input makes a broken terminal far worse than
in 001: without restoration, the user's shell is left unechoed and
unbuffered, so they cannot see what they type to fix it.

**Independent Test**: On POSIX, a pseudo-terminal test starts a child
that creates a system terminal and then is ended by each handled signal;
the test compares the pseudo-terminal's modes and the bytes written with
those before create, and the child's exit status with the status it has
without Overdrive. On Windows, the manual checklist.

**Acceptance Scenarios**:

1. **Given** a running program with a system terminal and no signal
   handler of its own, **When** it receives a termination signal or a
   crash signal, **Then** the
   terminal is restored and the process then ends as that signal would
   have ended it without Overdrive (same exit status, core dump if the
   default action dumps core).
2. **Given** a system terminal with input enabled, **When** the user
   presses Ctrl+C, **Then** the program receives a Ctrl+C key event and
   keeps running; no signal is raised. The same holds for Ctrl+\ and
   Ctrl+Z.
3. **Given** the application installed its own handler for a signal
   before create, **When** that signal arrives, **Then** the
   application's handler still runs (FR-032 says when the terminal is
   restored first).
4. **Given** the process is killed by a signal that cannot be caught,
   **Then** nothing is restored, and the documentation says so.
5. **Given** a system terminal created with abnormal-exit restoration
   turned off, **When** a handled signal arrives, **Then** Overdrive
   installed no handler, and the signal acts as without Overdrive.

---

### User Story 4 — Mouse input (Priority: P2)

A developer enables mouse reporting and receives presses, releases,
wheel steps, and drags, each with its cell position and modifiers.

**Why this priority**: Clickable panels are expected of a terminal UI,
but the first consumer works keyboard-only.

**Independent Test**: Headless decoding tests; manual checklist.

**Acceptance Scenarios**:

1. **Given** mouse reporting enabled, **When** the left button is
   pressed at column 10, row 3 (0-based), **Then** the event is a left
   press at (10, 3).
2. **Given** the same, **When** the wheel scrolls up, **Then** the event
   is one wheel-up step at the pointer's cell.
3. **Given** the same, **When** the pointer moves with a button held,
   **Then** drag events report each new cell.
4. **Given** a position beyond column 223 or row 223, **Then** it is
   still reported exactly.
5. **Given** mouse reporting disabled (the default), **Then** the
   terminal's own selection and scrolling work as without Overdrive.

---

### User Story 5 — Paste and focus (Priority: P2)

A developer receives pasted text as one paste, not as typed keys, so a
pasted line containing Enter does not trigger commands; and is told when
the window gains or loses focus.

**Why this priority**: Paste-as-keys is a correctness and safety hazard
for any app that binds Enter or single letters; focus is cheap once the
decoder exists.

**Independent Test**: Headless decoding tests; manual checklist.

**Acceptance Scenarios**:

1. **Given** bracketed paste enabled, **When** the user pastes
   "a\nb", **Then** the program receives a paste whose text is "a\nb"
   and no Enter key event.
2. **Given** a paste longer than any internal buffer, **Then** its text
   arrives completely and in order, its start and end are marked, and
   memory use stays bounded.
3. **Given** focus reporting enabled, **When** the window loses and
   regains focus, **Then** a focus-out event and then a focus-in event
   arrive.

### Edge Cases

- Input bytes that are invalid or truncated UTF-8: each maximal subpart
  of an ill-formed subsequence is one U+FFFD text key, as in 001 (R17).
- A text key or paste text containing C0/C1 controls, DEL, or ESC
  inside a paste: delivered as data in the event, never written to the
  terminal; drawing it later is subject to 001's FR-032.
- A paste that never ends (no end marker): its text is still delivered
  as it arrives; the decoder does not hold it without bound.
- A paste end marker inside the pasted text: the paste ends there (the
  terminal's protocol cannot tell them apart); documented.
- An escape sequence split across two reads: decoded as one event.
- An escape sequence longer than the longest one the decoder knows:
  discarded without blocking later input.
- A lone ESC followed by another key within the escape delay: decoded as
  Alt plus that key, as terminals encode it.
- Mouse coordinates outside the current size (a resize raced the
  report): reported as received; the documentation says they can exceed
  the size.
- Input disabled: no input mode changes, no input is read, and the input
  calls report that input is not enabled.
- stdin not a terminal with input enabled: create fails, nothing is
  changed (001's echo step is skipped in that case; enabling input is an
  explicit request, so it fails instead).
- An asynchronous signal (one sent by another process, the terminal,
  or `kill`) arriving during create or destroy, on the thread calling
  it: the terminal ends in a restored state, never half-changed. The
  same signal delivered to another thread in that window, and a
  synchronous fault raised by create or destroy itself, are documented
  as not covered (FR-034).
- A second signal arriving while restoration from the first runs:
  restoration is not run twice concurrently, and the process still ends.
- A crash after `ovd_terminal_destroy` already ran: nothing is written.
- Two system terminals (001 R29): abnormal-exit restoration restores the
  state before the first create.
- The application replacing Overdrive's signal handler after create:
  documented as disabling abnormal-exit restoration for that signal;
  destroy leaves the application's handler in place (FR-032).
- A 1×1 terminal works. A resize to 0 in either dimension while
  waiting: no resize event, since the size cannot be read (001 R25); the
  next `ovd_frame_begin` fails as in 001.
- Several resizes before the application waits: one resize event, with
  the size read last.

## Requirements *(mandatory)*

### Functional Requirements

**Input acquisition (Platform)**
- **FR-001**: Input MUST be opt-in per terminal at create. With it off,
  a terminal's input modes and rendering MUST be exactly feature 001's;
  abnormal-exit restoration is a separate option (FR-035).
- **FR-002**: With input on, the system backend MUST put the input side
  in raw mode: keys arrive unbuffered and unechoed, without line
  editing, and the keys that normally raise signals (Ctrl+C, Ctrl+\,
  Ctrl+Z) arrive as key events instead. Every mode changed MUST be read
  first and restored at destroy, in reverse order (extends 001 FR-011).
- **FR-003**: Mouse reporting, bracketed paste, and focus reporting MUST
  each be separately enabled at create, off by default, and turned off
  at destroy.
- **FR-004**: The headless backend MUST accept input bytes from the
  caller and decode them exactly as the system backend decodes bytes
  read from the terminal (extends 001 FR-012).

**Reading events (Input)**
- **FR-010**: The application MUST be able to wait for the next event
  with a timeout: zero (poll), a duration, or indefinitely. A wait MUST
  return as soon as an event is available.
- **FR-011**: Events MUST be one of a closed set: key, mouse, paste,
  focus, resize. A key MUST carry either a named key or one Unicode
  scalar value, and a set of modifiers (Shift, Alt, Ctrl).
- **FR-012**: The decoder MUST recognize the key sequences of xterm's
  documented legacy and modifier encodings (CSI and SS3 forms) and the
  sequences Windows Terminal sends, for the keys of US2 scenario 2.
- **FR-013**: A lone ESC MUST be reported as the Escape key once no
  further byte arrives within the escape delay. The delay MUST be
  configurable at create.
- **FR-014**: Unrecognized or malformed sequences MUST be discarded
  without blocking, delaying, or altering the events after them.
- **FR-015**: Mouse events MUST use 0-based cell coordinates and the
  SGR extended mouse encoding, so positions are exact at any size.
- **FR-016**: A paste MUST be delivered with its start and end marked
  and its text complete and in order, using bounded memory whatever its
  length; FR-018 is the measure of bounded.
- **FR-017**: A resize MUST be reported as an event while the
  application waits. The next `ovd_frame_begin` still re-reads the size
  (001 FR-014).
- **FR-018**: Reading input MUST NOT allocate after create, whatever the
  input contains (extends 001 SC-004).
- **FR-019**: Every byte read from the terminal MUST be treated as
  untrusted: every read and decode is bounded, and no input byte is
  written to the terminal (Art. XIII).

**Abnormal-exit restoration (Platform)**
- **FR-030**: When a system terminal exists, the process ending through
  a catchable termination or crash signal (POSIX: `SIGHUP`, `SIGINT`,
  `SIGQUIT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`, `SIGBUS`, `SIGFPE`,
  `SIGILL`), or on Windows through a console close event, Ctrl+C (only
  while input is off; with input on it is a key), Ctrl+Break, or an
  unhandled exception, MUST restore the terminal first. It applies
  whether or not input is enabled.
- **FR-031**: Abnormal-exit restoration MUST restore what destroy
  restores, using only operations that are safe at that point (POSIX
  async-signal-safe functions), and MUST then let the process end as it
  would have without Overdrive.
- **FR-032**: Handlers the application installed before create MUST
  still run. Destroy MUST put back, for each signal whose handler is
  still Overdrive's, exactly the handler installed before create; a
  handler the application installed after create is left in place. For
  a crash signal, restoration runs first,
  then the application's handler. For a termination signal the
  application already handles, the application has claimed it:
  Overdrive calls its handler and restores nothing, since the
  application may keep running and call destroy itself. A signal that
  was ignored before create stays ignored. On Windows, Ctrl+C and
  Ctrl+Break are termination signals for this rule, and a handler the
  application installed through the C runtime's `signal` (`SIGINT`,
  `SIGBREAK`) counts as handling them.
- **FR-033**: Restoration MUST run at most once per terminal, whichever
  of destroy, a signal, or a crash comes first.
- **FR-034**: What cannot be restored MUST be documented: uncatchable
  signals, power loss, the terminal emulator itself closing on POSIX
  after the process cannot write, modes another program changed, a
  normal exit (`exit`, returning from `main`) without destroy, a
  signal delivered to another thread while create or destroy runs, a
  synchronous fault (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`) raised on
  the thread running create or destroy, on macOS a crash handler
  installed with `SA_RESETHAND` that returns from a real fault (the
  platform does not report the flag, research R121), and, on Windows,
  logoff and shutdown events (only services receive them) and a console
  control handler the application registered directly before create (it
  runs after Overdrive has restored). On Windows the C runtime's console
  handler decides the claim (research R113): when the application set a
  C runtime handler for Ctrl+C or Ctrl+Break before create, those keys
  restore even though it handles them; an application Ctrl+Break handler
  also claims console close; and a Ctrl+C arriving during create may
  miss the application's handler.
- **FR-035**: Abnormal-exit restoration MUST be a create option of the
  system backend, on by default; turned off, create MUST install no
  abnormal-exit handler. Input's `SIGWINCH` handler (FR-036) belongs to
  input, not to restoration, and is installed whenever input is on. The
  headless backend MUST never install any handler.
- **FR-036**: Signal and console-control handlers, and the state they
  need, are process-wide. This is the one exception to 001 FR-002,
  limited to system terminals that have abnormal-exit restoration on or
  input on (on POSIX, resize is reported through a `SIGWINCH` handler),
  and MUST be documented at the create options. Headless terminals keep
  no process-wide state.

**Identity**
- **FR-040**: 001 FR-040 to FR-042 apply unchanged: `ovd_` symbols only,
  no OS header in a public header, nothing consumer-specific.

### Key Entities

- **Event**: one thing the user did: key, mouse, paste, focus, or
  resize, with its data.
- **Key**: a named key (arrows, Home, End, Page Up/Down, Insert, Delete,
  F1–F12, Tab, Backspace, Enter, Escape) or one Unicode scalar value,
  plus modifiers.
- **Mouse action**: press, release, drag, or wheel step; a button; a
  cell position; modifiers.
- **Paste**: a start marker, text, and an end marker.
- **Input configuration**: whether input is on, which reports are on
  (mouse, paste, focus), and the escape delay; part of the terminal's
  create configuration.
- **Restoration record**: what the terminal's create changed, kept in a
  form abnormal-exit restoration can replay.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every sequence in the decoder's test table (all keys of
  US2 scenario 2 with every modifier combination the encodings allow,
  mouse, paste, focus) decodes to its expected event: 100% of entries.
- **SC-002**: The decoder fed arbitrary bytes, under the address and
  undefined-behavior sanitizers, for at least 10⁷ bytes of random and
  mutated input, never crashes, never reads out of bounds, never stops
  returning later valid events, and never allocates.
- **SC-003**: For each signal in FR-030, the pseudo-terminal test's
  child ended by that signal leaves the terminal modes equal to those
  before create, and exits with the same status as the same child
  without Overdrive: 100% of signals, on Linux and macOS.
- **SC-004**: Reading input makes 0 allocator calls after create
  (measured by the allocation test).
- **SC-005**: An event already available is returned without waiting
  for the timeout; only a lone ESC waits, and for no longer than the
  escape delay.
- **SC-006**: The input example passes its manual checklist (keys,
  mouse, paste, focus, resize, Ctrl+C, closing the window, a crash) in
  Windows Terminal and one POSIX terminal, with versions recorded.
- **SC-007**: 001 SC-005 and SC-006 still hold: 0 warnings on the
  declared matrix, 0 exported symbols without `ovd_`.

## Assumptions

- Consumers are single-threaded with respect to one terminal (001); no
  call wakes a wait from another thread.
- Input comes from the same terminal as output: stdin on POSIX, the
  console input buffer on Windows.
- Only the encodings terminals send without being asked are decoded,
  plus those the enabled reports request (SGR mouse 1006, bracketed
  paste 2004, focus 1004). Enhanced keyboard protocols (kitty keyboard,
  xterm modifyOtherKeys) need capability queries, which stay out of
  scope.
- Keys that the legacy encodings cannot distinguish (Ctrl+I and Tab,
  Ctrl+M and Enter, Ctrl+[ and Escape, Ctrl+H and Backspace on some
  terminals) are reported as the second of each pair; documented.
- Windows support starts at Windows 10 1809, as in 001.
- Before create, mouse reporting, bracketed paste, and focus reporting
  are off (the normal shell state), since they cannot be read back.

## Out of scope

- Enhanced keyboard protocols, capability and version queries.
  **Stated future direction** (owner, 2026-11-05): the kitty keyboard
  protocol and terminal graphics (kitty graphics, sixel) will be
  supported in a later feature. Nothing is built for them here
  (Art. VII), but 002's choices must not preclude them: query replies
  and kitty keyboard reports arrive as input sequences, so the decoder
  must be able to recognize new sequence forms without changing the
  event contract for existing ones.
- Waking a wait from another thread, or integrating with an external
  event loop through a file descriptor or handle.
- Key bindings, key-chord parsing, text editing, IME composition.
- Mouse motion without a button held (hover).
- Job-control suspend and resume (`SIGTSTP`, `SIGCONT`). With input on,
  Ctrl+Z arrives as a key, so the application is not suspended by it;
  a `SIGTSTP` sent another way acts as without Overdrive.
- Ctrl+C as an interrupt while input is on: it is a key event (FR-002).
  An application that wants to quit on it does so on the key.
- Layout, Motion, Widgets (unchanged from 001).
- Integration into any consuming application.

## Dependencies on decisions outside this spec

- Feature 001 merged (done).

## Clarifications

### Session 2026-11-05

- Q: How is abnormal-exit restoration turned on, given 001 FR-002? → A:
  A system-backend create option, on by default (FR-035, FR-036).
- Q: What does Ctrl+C do in raw mode? → A: It is a key event; no signal
  (FR-002, US3 scenario 2).
- Q: Is job-control suspend and resume in scope? → A: No (Out of scope).
