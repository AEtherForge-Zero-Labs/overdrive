# Research: Input and Abnormal-Exit Restoration

Decision log for `specs/002-input-restore`. Decisions are numbered R101
onward so they never collide with 001's R1–R48, which code comments
cite. Each one carries a provenance tag (CLAUDE.md rule 4):

- **VERIFIED**: read in the cited standard or vendor page while writing
  this file;
- **INFERRED**: follows from a VERIFIED source but is not stated in it;
- **ASSUMED**: from memory or general practice, to be checked by the
  implementing issue against the cited source before the code depends on
  it. An ASSUMED item that turns out false is a halt condition for that
  issue (Art. XII), not a silent redesign.

Sources:
- [POSIX] POSIX.1-2024, https://pubs.opengroup.org/onlinepubs/9799919799/
  (§2.4.3 Signal Actions; `termios.h`, `sigaction`, `poll`, `pipe`).
- [XTERM] xterm control sequences,
  https://invisible-island.net/xterm/ctlseqs/ctlseqs.html (already cited
  by 001 R26).
- [MS-MODE] https://learn.microsoft.com/windows/console/setconsolemode
- [MS-VT] https://learn.microsoft.com/windows/console/console-virtual-terminal-sequences
- [MS-CTRL] https://learn.microsoft.com/windows/console/setconsolectrlhandler
  and `handlerroutine`.
- [MS-INPUT] https://learn.microsoft.com/windows/console/readconsoleinput
  and `input-record-str`.

## A. Clarifications carried in from the spec

- Restoration is a system-backend create option, on by default (spec
  FR-035). Ctrl+C is a key in raw mode (FR-002). Suspend and resume are
  out of scope.
- Owner's stated future direction: kitty keyboard protocol and terminal
  graphics (kitty graphics, sixel). R108 says what 002 does about it.

## B. Decisions

**R101 — One new public header, `input.h`.** The Input module of
`OVD-IDENTITY-001` §4.7 becomes a header: event types and two
functions, `ovd_input_wait` and `ovd_input_feed`. Config fields go into
the existing `OvdTerminalConfig` (terminal.h), because input is acquired
at create with the other modes (FR-001).
*Rejected:* a separate `OvdInput` object created from a terminal. Input
shares the terminal's lifetime, handles, mode record, and allocator; a
second object adds a lifetime rule with no requirement behind it
(Art. VII).

**R102 — Wait returns one event; a timeout is success.**
`ovd_input_wait(terminal, timeout_ms, &event)` returns `OVD_OK` and
either an event or `OVD_EVENT_KIND_NONE` when the timeout elapsed. The timeout
is `int32_t` milliseconds: 0 polls, positive waits at most that long,
`OVD_WAIT_FOREVER` (-1, any negative value) waits until an event. A
timeout is not an error, so `OvdResult` gains no value. A wait is
allowed with or without an open frame (input does not touch the
canvas). Called from inside the terminal's sink it returns
`OVD_ERROR_STATE`, extending 001 R24's list of busy calls, because the
sink runs inside `ovd_frame_end`.
*Rejected:* a new `OVD_ERROR_TIMEOUT`: callers would have to treat one
error as normal flow. A callback-based event loop: Overdrive would own
the application's loop (out of scope, spec).

**R103 — Paste is three event kinds.** `OVD_EVENT_KIND_PASTE_START`,
`OVD_EVENT_KIND_PASTE_TEXT` (a chunk), `OVD_EVENT_KIND_PASTE_END`. Chunk text
points into the terminal's paste chunk buffer and is valid until the next
`ovd_input_wait`, `ovd_input_feed`, or destroy on that terminal. This
is how FR-016 gets bounded memory for any length (US5 scenario 2).
Chunks are valid UTF-8: ill-formed bytes become U+FFFD by 001's R17
rule, reusing `unicode.c`'s decoder. A chunk never ends inside a UTF-8
sequence or a possible end marker; up to 5 bytes (`ESC [ 2 0 1 ~` less
its last byte) and one partial scalar are held back.
*Rejected:* one paste event with all the text: needs unbounded memory or
truncation. Raw bytes without UTF-8 repair: consumers would each need to
validate.

**R104 — The decoder is an owning module over a generic CSI parse.**
`src/decode.{c,h}`, struct `OvdDecoder`, embedded in `OvdTerminal`. It
holds the partial-sequence state across reads (a sequence split across
two reads is one event, spec edge cases). Bytes are parsed in two
layers:
1. ECMA-48 syntax: C0 controls, `ESC` + byte, `SS3` + byte, and
   `CSI` *parameter bytes* (0x30–0x3F) *intermediate bytes* (0x20–0x2F)
   *final byte* (0x40–0x7E) (ASSUMED: ECMA-48 §5.4 ranges, also given in
   [XTERM]'s introduction). The parameter area is bounded (64 bytes);
   longer sequences are consumed to their final byte and discarded
   (FR-014). A byte outside the grammar ends the sequence and is
   decoded afresh.
2. A dispatch table from (prefix, final byte, parameter shape) to an
   event. Unknown entries are discarded (FR-014).
This is also what R108 needs. Paste mode bypasses layer 2: bytes are
text until `CSI 201 ~`.

**R105 — The key encodings decoded.** From [XTERM], "PC-Style Function
Keys" and "Mouse Tracking" (ASSUMED until the decode issue quotes each
row into its test table):
- `CSI A/B/C/D/H/F` and `SS3 A/B/C/D/H/F`: arrows, Home, End;
  `CSI 1 ; m X` adds modifiers.
- `CSI n ~`: 2 Insert, 3 Delete, 5 PageUp, 6 PageDown, 1/7 Home,
  4/8 End, 15 17 18 19 20 21 23 24 F5–F12; `CSI n ; m ~` with modifiers.
- `SS3 P/Q/R/S` and `CSI 1 ; m P/Q/R/S`: F1–F4.
- `CSI Z`: Shift+Tab.
- Modifier parameter m = 1 + bits (1 Shift, 2 Alt, 4 Ctrl, 8 Meta);
  Meta is dropped (not in the spec's modifier set).
- C0: 0x0D Enter, 0x09 Tab, 0x7F and 0x08 Backspace, 0x1B Escape (after
  the delay), 0x00 Ctrl+Space, 0x01–0x1A Ctrl+a..z except those, 0x1C–0x1F
  Ctrl+\ ] ^ _. The legacy pairs the spec lists as indistinguishable
  are reported as the named key (Tab, Enter, Escape, Backspace).
- `ESC` + key within the delay: Alt + that key. `ESC [` and `ESC O`
  with nothing after them within the delay are Alt+[ and Alt+O: the
  only way to tell them from an unfinished CSI or SS3.
- Windows Terminal sends the same xterm forms through VT input (ASSUMED,
  [MS-VT] "Input Sequences"; checklist item).

**R106 — Raw mode on POSIX.** Added as a new R28-style step after
echo, read before change, restored in reverse: clear `ICANON`, `ISIG`,
`IEXTEN` in `c_lflag`; clear `IXON` and `ICRNL` in `c_iflag` (so
Ctrl+S, Ctrl+Q arrive and Enter is CR, distinct from Ctrl+J); set
`VMIN` 0 and `VTIME` 0. `OPOST` is kept: output processing is not input
(ASSUMED meanings, [POSIX] `termios.h`; the existing echo step already
cites it). Read back after set, as the echo step does.
*Rejected:* `cfmakeraw`: not in POSIX, and it clears `OPOST`.

**R107 — Raw mode on Windows: VT input through ReadConsoleInputW.**
Input mode: clear `ENABLE_LINE_INPUT`, `ENABLE_ECHO_INPUT`,
`ENABLE_PROCESSED_INPUT` (so "CTRL+C is ... placed in the input
buffer", VERIFIED [MS-MODE]); set `ENABLE_VIRTUAL_TERMINAL_INPUT`
("convert user input ... into Console Virtual Terminal Sequences",
VERIFIED [MS-MODE]) and `ENABLE_WINDOW_INPUT`; when mouse is on, set
`ENABLE_EXTENDED_FLAGS` without `ENABLE_QUICK_EDIT_MODE` ("To disable
this mode, use ENABLE_EXTENDED_FLAGS without this flag", VERIFIED
[MS-MODE]). Resize is reported only to `ReadConsoleInput` ("not by
those using ReadFile or ReadConsole", VERIFIED [MS-MODE]), so input is
read with `ReadConsoleInputW`: `KEY_EVENT` records with key-down carry
the VT sequence's UTF-16 units (ASSUMED [MS-INPUT]; checklist), which
are converted to UTF-8 and fed to the same decoder;
`WINDOW_BUFFER_SIZE_EVENT` triggers a size re-read, reported as resize.
Waiting uses `WaitForSingleObject` on the input handle (ASSUMED).
*Rejected:* native `KEY_EVENT`/`MOUSE_EVENT` decoding: a second decoder
that the headless backend cannot exercise (FR-004 parity), and mouse
coordinates relative to the buffer rather than the window.

**R108 — Not precluding kitty keyboard and graphics.** Owner's future
direction (spec, Out of scope). 002 builds nothing for it (Art. VII).
What keeps the door open, all of which 002 needs anyway:
- R104's layer 1 parses every CSI form, including `CSI ? … u` (kitty
  flags reply) and `CSI … u` (kitty key reports) and DA replies, so a
  later feature adds dispatch rows, not a new parser (ASSUMED forms).
- `OvdKey` is a fixed-type enum with room to grow; `OvdEvent` is
  transparent but read through `kind`, so new kinds are additive. 0.x
  promises no ABI (Art. XIV).
- Graphics are output-side; they need capability queries, whose replies
  arrive through this decoder. Nothing in 002's output path changes.
*Rejected:* adding an `OVD_EVENT_KIND_UNKNOWN` raw-sequence event now "for
future protocols": no present consumer (Art. VII).

**R109 — Escape delay: a config field, 50 ms default, 0–1000.**
FR-013. Measured from the arrival of the lone ESC with a monotonic
clock (POSIX `clock_gettime(CLOCK_MONOTONIC)`, Windows
`GetTickCount64`; ASSUMED). The default is a starting value, not a
measured target (Art. XV); the checklist records whether Alt+key and
Escape are both reliable in Windows Terminal at 50 ms.
*Rejected:* no delay (ESC immediately): Alt+key and split sequences
break. 
**Headless timing is virtual.** A headless wait never sleeps: it treats
its own timeout as the time that passed. A pending lone ESC resolves
when one wait's timeout is at least the delay, or a forever wait is
called with nothing else queued. A forever wait with nothing queued
returns `OVD_EVENT_KIND_NONE` (nothing can arrive). Tests are deterministic
without a clock.

**R110 — Headless feed.** `ovd_input_feed(terminal, bytes, length,
&accepted)` copies up to the free space of a fixed input queue
(`OVD_INPUT_QUEUE_SIZE`, 4096 bytes, allocated at create) and reports
how many it took. Headless only (`OVD_ERROR_UNSUPPORTED` otherwise),
`OVD_ERROR_STATE` if input is off. A pending headless `set_size` is
reported by the next wait as a resize event, as the system backend
reports a real resize.

**R111 — POSIX resize wakeup: SIGWINCH and a self-pipe.** `poll` waits
on stdin and the read end of a non-blocking pipe; the `SIGWINCH`
handler writes one byte to the write end. `write`, `read`, `poll` are
async-signal-safe (VERIFIED [POSIX] §2.4.3). The pipe exists only for a
system terminal with input on. Process-wide (spec FR-036). Both ends
are `O_NONBLOCK` (a full pipe drops the byte; one pending byte is
enough) and `FD_CLOEXEC` (a child process does not inherit them). The
handler is installed with `SA_RESTART`, so other calls in the
application are not interrupted by resizes, and calls the previous
`SIGWINCH` handler (spec FR-032). The self-pipe also makes the wakeup
independent of whether `SA_RESTART` restarts `poll`.
Resize reporting, both platforms: a wakeup drains the pipe (or the
Windows resize records) and re-reads the size. A size that differs
from the last one reported becomes one `OVD_EVENT_KIND_RESIZE`; several
wakeups before a wait coalesce into one event with the last size read;
a size that cannot be read or reads as 0 (001 R25) produces no event.
*Rejected:* `ppoll`/`pselect` with a signal mask: `ppoll` is POSIX.1-2024
but not on the macOS SDK, and macOS's `pselect` is not atomic (both
ASSUMED). Polling the size on a short timer: wakeups while idle, and
latency up to the period.

**R112 — POSIX abnormal-exit restoration.** One process-wide record
(static storage) with an atomic state, lock-free (`atomic_int`,
checked with `ATOMIC_INT_LOCK_FREE == 2`): `IDLE` → `ARMED` → `CLAIMED`
→ `DONE`, and back to `IDLE` at destroy. C23 allows a signal handler to
use lock-free atomic objects (as 001 R48 relies on).
*Create* (system backend, `restore_on_exit` on), with the handled
signals blocked in the calling thread (`pthread_sigmask`) from its first
mode change to its last step, so a signal arriving on that thread is
delivered only once the record is armed:
1. if the state is not `IDLE`, another system terminal owns the record
   (001 R29): this one registers nothing, and the state before the first
   create is what gets restored (spec edge case);
2. fill the record: the termios to restore, the exit sequence bytes
   (R116), and the previous `sigaction` of each handled signal;
3. store `ARMED` (release), then install the handler with `sigaction`
   for each signal of spec FR-030 whose previous disposition was not
   `SIG_IGN`, with `SA_SIGINFO` and an `sa_mask` holding every handled
   signal, so a second one waits until the first handler ends.
*Handler* (async-signal-safe calls only; VERIFIED [POSIX] §2.4.3 lists
`write`, `tcsetattr`, `sigaction`, `raise`):
1. a termination signal (`SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGTERM`)
   whose previous action is a handler: call that handler and return;
   restore nothing (spec FR-032);
2. otherwise, compare-exchange `ARMED` → `CLAIMED`. If it won: `write`
   the exit sequence, `tcsetattr` the saved termios, store `DONE`. If
   the state is `CLAIMED`, another thread is restoring: spin until
   `DONE` (the claimer cannot be this thread: the claiming thread has
   these signals blocked, by `sa_mask` or by destroy's mask). If `DONE`
   or `IDLE`, skip;
3. call the previous handler if it was one, in the form its flags
   declare (`sa_handler`, or `sa_sigaction` with `SA_SIGINFO`);
4. reset the signal to `SIG_DFL` and `raise` it, then return. The signal
   is blocked while its handler runs, so the raised one is pending and
   is delivered under the default action when the handler returns; a
   hardware fault that is not, re-faults under it. Both keep the exit
   status and core dump. The same path covers a crash signal sent with
   `kill`, which a bare return would survive (ASSUMED: POSIX leaves
   returning from a real fault's handler undefined; SC-003's test is the
   per-platform proof).
*Destroy*, with the handled signals blocked in the calling thread:
compare-exchange `ARMED` → `CLAIMED`; if it won, restore as now and
store `DONE`; if a handler holds `CLAIMED`, wait for `DONE`. Then, for
each handled signal whose current action is still Overdrive's handler
(read with `sigaction`), put back the previous one; one the application
installed after create stays (spec FR-032). Then store `IDLE` and
unblock; a signal that arrived meanwhile takes its restored disposition
on a terminal already restored. Restoration therefore runs exactly once
(FR-033) and is never interrupted half-way on the calling thread.
*Not covered*, documented (spec FR-034): the same signal delivered to
another thread between create's first mode change and `ARMED`, or
during destroy after `IDLE`.
*Partial frame:* a handler can interrupt a frame's write in the middle
of a control sequence, so the exit sequence starts with CAN (0x18),
which cancels a sequence in progress (ASSUMED: ECMA-48 §8.3.6 and
[XTERM] "Single-character functions"; the pty test interrupts a frame
write to check it).
*Rejected:* `atexit`: cannot be unregistered, runs after the
application may have freed the terminal, and the spec lists a normal
exit without destroy as not restored (FR-034). Skipping the record when
it is already claimed, without waiting: a signal on another thread would
end the process while destroy is half-way. `sigprocmask`: unspecified in
a multi-threaded process ([POSIX] `sigprocmask`; ASSUMED wording).

**R113 — Windows abnormal-exit restoration.** `SetConsoleCtrlHandler`
for `CTRL_BREAK_EVENT`, `CTRL_CLOSE_EVENT`, `CTRL_LOGOFF_EVENT`,
`CTRL_SHUTDOWN_EVENT`; the handler restores (write exit sequence,
`SetConsoleMode` both handles, `SetConsoleOutputCP`) and returns FALSE
so the next handler or the default runs (ASSUMED [MS-CTRL]: the handler
runs on a new thread; returning FALSE passes the event on). Crashes:
`SetUnhandledExceptionFilter`, saving and chaining the previous filter,
and the CRT `signal(SIGABRT, …)` for `abort` (ASSUMED). R112's record
and state machine are shared: the console handler runs on its own
thread, so a `CLAIMED` state held by destroy is waited for, never
skipped. The handler may still interleave with a frame write on the
main thread; the exit sequence's CAN prefix (R112) cancels a sequence
cut in half, and the checklist observes the result. Create installs the
console handler with `SetConsoleCtrlHandler` only after `ARMED`; destroy
removes it (`SetConsoleCtrlHandler(handler, FALSE)`) and restores the
previous exception filter only if the current one is still Overdrive's
(ASSUMED: `SetUnhandledExceptionFilter` returns the previous filter, so
the check is a second call that puts the observed one back).

**R114 — Allocation.** Create allocates, through the terminal's
allocator, the input queue (R110; on the system backend the read
buffer) and the paste chunk buffer, each fixed size. The decoder's
state is inline. `ovd_input_wait` and `ovd_input_feed` never allocate
(FR-018, SC-004). The process-wide restoration record is static
storage, not allocated.

**R115 — Validation of config.** `mouse`, `bracketed_paste`,
`focus_events` with `input` off → `OVD_ERROR_INVALID_ARGUMENT`.
`escape_delay_ms` outside 0–1000 → `OVD_ERROR_INVALID_ARGUMENT`, even
with input off (as 001 validates `color_depth` when detection overrides
it).
`restore_on_exit` is ignored by the headless backend (it never installs
handlers, FR-035), like `alternate_screen`. System backend with `input`
on and stdin not a terminal → `OVD_ERROR_NOT_A_TERMINAL` (spec edge
case), nothing changed.
Until the input layers land, create with `input` true returns
`OVD_ERROR_UNSUPPORTED` (T103): a loud interim result, removed for the
headless backend by T113 and for the system backend by T116, never a
silently ignored field.

**R116 — Enter and exit sequences for the reports.** Enter, appended to
001's enter sequence in the same write: `CSI ? 1002 h` (button-event
tracking: press, release, drag) and `CSI ? 1006 h` (SGR coordinates)
when mouse is on; `CSI ? 2004 h` for bracketed paste; `CSI ? 1004 h`
for focus. Exit, in 001's exit write, after the CAN prefix (R112) and
before leaving the alternate screen: the matching `l` forms, in reverse
order (ASSUMED [XTERM]
"Mouse Tracking", "Bracketed Paste Mode", `FocusIn/FocusOut`). Hover
(1003) is out of scope. These are written unconditionally at exit only
for reports that were on: they cannot be read back before create (spec
Assumptions).

**R117 — Tests.** All headless first (Art. V):
- `tests/test_decode.c`: SC-001's table, one row per sequence, written
  as readable notation (`"\x1b[1;5C" -> "key Right +ctrl"`), Art. VI.
- `tests/test_decode_fuzz.c`: SC-002, a seeded PRNG, 10⁷ bytes of random
  and mutated table sequences, with the decoder's invariants checked
  after each step; run under a new `sanitize` preset (ASan + UBSan),
  Linux and macOS CI.
- `tests/test_input.c`: wait/feed state machine, escape delay, paste
  chunking, headless resize event, config validation.
- `tests/test_steady_state.c` extended: input on, no allocation in wait/feed.
- `tests/test_restore_pty.c` (POSIX): SC-003, fork per signal; the
  parent holds the pty, compares termios and the exit-sequence bytes,
  and compares `waitpid` status with a control child without Overdrive.
- `tests/test_system_pty.c` extended: raw-mode flags during the run and
  restored after destroy.
- `examples/input_demo.c` (name from `OVD-IDENTITY-001` §8.3) and new
  items in `docs/validation/manual-checklist.md`: SC-006.

## C. Tensions

| Articles | Tension | Resolution |
|---|---|---|
| 001 FR-002 vs spec FR-035/036 | Signal handlers are process-wide | One static record, documented at the config fields; headless never touches it; owner chose default-on |
| Art. III vs modifier sets | C cannot type a set of enum bits | Same as 001's `OvdAttr`: `OvdModifier` enum for values, `uint8_t` set, documented at the field |
| Art. IV vs `OvdEvent` transparency | A tagged union exposes representation | Plain value with no invariant beyond "read the member `kind` names"; avoids N accessor calls per event, like `OvdCellView` |
| Art. VII vs kitty/sixel direction | Owner wants them later | R108: build nothing; generic CSI parse is needed now for FR-014 |
| Art. XIII vs paste text | Pasted bytes are untrusted data handed to the app | Valid UTF-8 guaranteed (R103); controls are kept as data, and 001 FR-032 still blocks them at draw time |
