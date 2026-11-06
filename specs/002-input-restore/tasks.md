# Tasks: 002-input-restore

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [quickstart.md](quickstart.md),
[contracts/](contracts/include/overdrive/)

Format: `- [ ] T### [P] [Story] Description naming the file (depends on …)`.
Task ids start at T101 and decisions at R101, so neither collides with
001's T001–T040 and R1–R48. `[P]` = parallel-safe with other `[P]` tasks
in its phase (disjoint files). Each phase maps to one GitHub issue
(`OVD-NN`, continuing 001's numbering; see [workgraph.json](workgraph.json));
once issues exist, lines gain `→ #<number>`. Every behavior task is
preceded by its test task, which must be seen failing (`AGENTS.md`,
Definition of done). A task that depends on an ASSUMED item in
`research.md` first checks it against the cited source and quotes it in
the PR; a false assumption halts the issue (Art. XII). Reviewers use
`specs/001-render-core/checklists/identity-review.md`.

## Phase 0 — OVD-15 Contract freeze and sanitizer preset (blocks everything)

- [x] T101 Copy `specs/002-input-restore/contracts/include/overdrive/input.h`, `terminal.h`, and `overdrive.h` into `include/overdrive/` unchanged (contract freeze point for 002), and add `input.h` to `overdrive_public_headers` in `CMakeLists.txt` (this issue owns that one line), so `test_headers` compiles it alone and twice and checks its includes (FR-040; 001 T009)
- [x] T102 Write the config tests in `tests/test_terminal.c`: `ovd_terminal_config_default` gives `input`, `mouse`, `bracketed_paste`, `focus_events` false, `escape_delay_ms` 50, `restore_on_exit` true; create rejects `mouse`, `bracketed_paste`, or `focus_events` without `input`, and `escape_delay_ms` of -1 and 1001 even with input off, with `OVD_ERROR_INVALID_ARGUMENT` and `*out` untouched (R115)
- [x] T103 Implement the defaults and R115 validation in `src/terminal.c`. Until OVD-17 and OVD-18 land, create with `input` true returns `OVD_ERROR_UNSUPPORTED` (a loud interim result, removed by those issues, never a silently ignored field); `restore_on_exit` is accepted and has no effect until OVD-19, which the issue body states
- [x] T104 [P] Add `sanitize` configure, build, and test presets to `CMakePresets.json` (inherits `debug`; AddressSanitizer and UndefinedBehaviorSanitizer through cache variables, recovery off) and Linux GCC 14 and macOS rows to `.github/workflows/ci.yml`, covered by `ci-ok` (SC-002's path; Art. VIII). Declare the sanitizer runtimes per platform in `.specify/memory/baseline.md` (GCC 14's libasan and libubsan on Linux; Apple Clang's own runtime on macOS, so the macOS row uses Apple Clang, not Homebrew GCC). Run the existing suite under it first: a failure it exposes in existing code is a separate bug issue, and this issue halts until that merges (Art. IX, XII)

## Phase 1 — OVD-16 Decoder (depends on OVD-15)

Goal: bytes in, typed events out, for every form the spec names, with
no terminal (US1, US4, US5). Independent test: `ctest -R test_decode`.

- [x] T105 [US1] Write `tests/test_decode.c`, the SC-001 table in readable notation (`"\x1b[1;5C" -> "key Right +ctrl"`, Art. VI), one row per sequence: every key of spec US2 scenario 2 in each CSI, SS3, and `CSI n ~` form with every modifier value 2–16 (R105, quoting each form from the xterm reference and from Microsoft's console virtual terminal input-sequence page into the PR); C0 keys including Ctrl+Space, Ctrl+a..z, Ctrl+\ ] ^ _, Ctrl+C as `c` +ctrl; the legacy pairs reported as the named key; UTF-8 text keys and U+FFFD for each maximal ill-formed subpart (001 R17); Alt+key, `ESC [` and `ESC O` alone as Alt+[ and Alt+O after the delay; lone ESC before and after the delay; unknown CSI with parameters and intermediates discarded; a sequence longer than the parameter bound discarded to its final byte; every sequence split at every byte boundary decodes the same (R104)
- [x] T106 [US4] Add the mouse rows to `tests/test_decode.c`: SGR `CSI < b ; x ; y M/m` press, release, drag (bit 32), wheel up and down (64, 65), Shift/Alt/Ctrl bits, 1-based to 0-based, coordinates above 223 and up to `INT32_MAX`, overflowing coordinates discarded (FR-015; R105, R116)
- [x] T107 [US5] Add the paste and focus rows to `tests/test_decode.c`: `CSI 200 ~` … `CSI 201 ~` gives START, TEXT chunks, END with no key events inside; text with C0 controls and ESC kept as data; an end marker split across reads; up to 5 held-back bytes never released early or lost; ill-formed UTF-8 in a paste repaired to U+FFFD; a chunk never ends inside a scalar value; a paste with no end marker still delivers its text; `CSI I` and `CSI O` focus in and out (FR-016; R103)
- [x] T108 [US1] Implement `src/decode.h` and `src/decode.c`: `OvdDecoder` (an owning module; data-model.md) with the ECMA-48 syntax layer, the dispatch table, paste mode, and the escape-delay rule taking elapsed time as an argument, so the module reads no clock (R104, R105, R109); reuses `src/unicode.c`'s UTF-8 decoder (Art. IX); appends `target_sources(overdrive PRIVATE src/decode.c)` to `CMakeLists.txt`
- [x] T109 [US1] Write `tests/test_decode_fuzz.c`: a seeded PRNG drives at least 10⁷ bytes of random bytes and mutated table sequences (splices, truncations, repeats) through the decoder in random-sized chunks; after every call it checks data-model.md's decoder invariants (bounds, progress, no key events in paste state) and that a known sequence fed afterwards still decodes; registered with `ovd_add_test`, so it runs under `debug` and `sanitize` (SC-002)

## Phase 2 — OVD-17 Headless input API (depends on OVD-16)

Goal: `ovd_input_wait` and `ovd_input_feed` on a headless terminal (US1,
US5). Independent test: `ctest -R 'test_input|test_steady_state'`.

- [x] T110 [US1] Write `tests/test_input.c`: input off → wait and feed return `OVD_ERROR_STATE`; nullptr arguments → `OVD_ERROR_INVALID_ARGUMENT`; feed reports `accepted` up to `OVD_INPUT_QUEUE_SIZE` and 0 when full; wait 0 with nothing queued → `OVD_EVENT_KIND_NONE`; headless virtual time: a lone ESC stays pending under timeouts below the delay and becomes Escape at a timeout ≥ the delay or a negative one; a negative timeout with nothing queued returns at once (R109); wait inside the sink → `OVD_ERROR_STATE` and works with a frame open (R102); `ovd_terminal_set_size` reported once as `OVD_EVENT_KIND_RESIZE` (R110); every signal disposition read with `sigaction` before a headless create with input on equals the one after destroy (FR-035, POSIX only); feed on a system terminal is covered by OVD-18
- [x] T111 [US5] Add paste lifetime tests to `tests/test_input.c`: a paste longer than three queues fed in pieces arrives whole and in order through START, TEXT, END; each chunk is valid until the next input call; memory is bounded (no allocation, T112) (FR-016; R103)
- [x] T112 [US1] Extend `tests/test_steady_state.c`: with input on, 10⁴ feed and wait calls over the decode table and pastes make 0 allocator calls (SC-004, FR-018); and in `tests/test_input.c`, the failing allocator at each of create's input allocations leaves nothing allocated and `*out` untouched (001 R21)
- [x] T113 [US1] Implement `src/input.c` (`ovd_input_wait`, `ovd_input_feed`, the queue, the paste chunk buffer sized per data-model.md) and, in `src/terminal.c` and `src/internal.h`, the input fields and their allocation at create when `input` is on; remove T103's interim `OVD_ERROR_UNSUPPORTED` for the headless backend; append `src/input.c` to `CMakeLists.txt` (FR-004, FR-010, FR-011, FR-017, FR-018; R102, R109, R110, R114)

## Phase 3 — OVD-18 Input on a real terminal: raw mode, wait, resize, reports (depends on OVD-17)

Goal: keys from the user's terminal on POSIX and Windows, and the report
modes turned on and off (US2, plus US4 and US5 enabling). Independent
test: `ctest -R test_system_pty` on Linux and macOS; Windows compiles and
its headless suite passes under Wine; real terminals are UNVERIFIED until
the checklist (OVD-20).

- [x] T114 [US2] Extend `tests/test_system_pty.c`: with input on, termios during the run has `ICANON ISIG IEXTEN ECHO IXON ICRNL INLCR IGNCR ISTRIP` clear, `VMIN` 0, `VTIME` 0, and `OPOST` as before, and after destroy equals the termios before create (R106); bytes written to the master arrive as events (`q`, an arrow, `0x03` as `c` +ctrl; that no `SIGINT` is raised is T117's check, since this pty is not the test's controlling terminal); a resize set with `TIOCSWINSZ` and delivered with `kill(getpid(), SIGWINCH)` (the pty is not the test's controlling terminal) wakes a forever wait with one resize event, and two resizes before a wait give one event; a `TIOCSWINSZ` of 0×N gives no event; timing (SC-005): a byte written while `ovd_input_wait(10000)` blocks returns its event within 1 s, and a lone ESC becomes Escape after the escape delay and within the delay plus 1 s; stdin on `/dev/null` with input on → `OVD_ERROR_NOT_A_TERMINAL` with nothing written or changed (R115); feed on a system terminal → `OVD_ERROR_UNSUPPORTED`; with mouse, paste, and focus on, the enter bytes end with `CSI ? 1002 h CSI ? 1006 h CSI ? 2004 h CSI ? 1004 h` and the exit bytes start with CAN and turn exactly those off in reverse order (R116); a `SIGWINCH` handler installed before create still runs and is back after destroy (FR-032)
- [x] T115 [US2] Extend `src/platform/platform.h`, `src/platform/platform_posix.c`, and `src/platform/platform_windows.c`: the raw-input step after echo, read before change and restored in reverse (R106, R107); a bounded read into the queue; a wait with a millisecond timeout on stdin and, on POSIX, the self-pipe (R111); a monotonic clock in milliseconds (R109); the `SIGWINCH` handler with `SA_RESTART`, chaining the previous one, `O_NONBLOCK` and `FD_CLOEXEC` pipe ends, put back at destroy only if still Overdrive's (R111). Windows reads `ReadConsoleInputW` records, converts key-down UTF-16 to UTF-8, and turns `WINDOW_BUFFER_SIZE_EVENT` into a size re-read (R107). Verify each call against POSIX and Microsoft's pages and cite them in comments, as 001's platform files do
- [x] T116 [US2] Wire the system backend in `src/terminal_system.c`, `src/input.c`, and `src/internal.h`: input on requires stdin to be a terminal (R115); the enter and exit sequences gain the reports and the CAN prefix (R112, R116); system wait reads, decodes, and coalesces resizes per R111; remove T103's interim `OVD_ERROR_UNSUPPORTED`; the terminal-mode table in plan.md matches the code, every row with its undo (CLAUDE.md, Repo specifics)

## Phase 4 — OVD-19 Abnormal-exit restoration (depends on OVD-18)

Goal: the terminal is restored on a termination or crash signal and the
process ends as it would have (US3). Independent test: `ctest -R
test_restore_pty` on Linux and macOS.

- [x] T117 [US3] Write `tests/test_restore_pty.c` (POSIX): for each signal of spec FR-030, fork a child on a fresh pty (its slave as the child's stdin, stdout, and controlling terminal after `setsid`; how the slave becomes the controlling terminal on Linux and macOS is ASSUMED until checked and cited), create a system terminal with input, mouse, paste, and focus on, and end it with the signal; the parent compares the slave's termios with that before create, checks the exit bytes (CAN, reports off, autowrap on, cursor visible, alternate screen left), and compares `waitpid` status (signal number, core flag) with a control child that does the same without Overdrive (SC-003). Cases: `0x03`, `0x1c`, and `0x1a` written to the master reach the child as Ctrl+C, Ctrl+\, and Ctrl+Z keys and the child keeps running (US3 scenario 2); a signal interrupting a frame write leaves the exit sequence decodable; an application `SIGTERM` handler installed before create runs and nothing is restored; one installed after create survives destroy; `SIGHUP` ignored before create stays ignored; `restore_on_exit` false leaves the disposition of every FR-030 signal as before create (with input on, only `SIGWINCH` is Overdrive's); a second system terminal restores the first one's state; a crash after destroy writes nothing; with an application `SIGSEGV` handler installed before create, a crash restores first and then runs the application's handler; a seeded loop of create and destroy with `SIGTERM` sent at a random delay always ends with the termios restored and the exit bytes written at most once (FR-031–FR-036, R112). Registered with `ASAN_OPTIONS` that leave these signals to the test (`handle_segv=0` and the like), so the `sanitize` preset tests Overdrive's handlers, not the sanitizer's
- [x] T118 [US3] Implement the restoration record, handler, and state machine in `src/platform/platform_posix.c` and `src/platform/platform.h` (R112): signals blocked in the calling thread over create and destroy with `pthread_sigmask`; `IDLE → ARMED → CLAIMED → DONE`; `SA_SIGINFO` handler chaining by the previous flags; reset to `SIG_DFL` and `raise`; put back only slots still Overdrive's. Verify each async-signal-safety and `pthread_sigmask` claim against POSIX.1-2024 and cite it
- [x] T119 [US3] Implement the Windows side in `src/platform/platform_windows.c` (R113): console control handler for close, Ctrl+C (arrives only with input off), and Ctrl+Break, with the claim rule left to the C runtime's own console handler, registered after Overdrive's by a `signal()` round trip for `SIGINT` and `SIGBREAK` (R113 option B); logoff and shutdown not registered (only services receive them); chained unhandled-exception filter; CRT `SIGABRT`; R112's shared state machine. Verify each call against Microsoft's reference and cite it; behavior in Windows Terminal is UNVERIFIED until OVD-20's checklist
- [x] T120 [US3] Wire it in `src/terminal_system.c` and `src/internal.h`: arm last at create, restore-then-put-back at destroy, the exit sequence built once into the record (R112, R116); a second system terminal does not register (data-model.md); `restore_on_exit` false installs no abnormal-exit handler (input's `SIGWINCH` handler is OVD-18's and stays), and the headless backend installs nothing (FR-035)

## Phase 5 — Demonstration and documentation

### OVD-20 input_demo and manual checklist [P] (depends on OVD-19)

- [x] T121 [P] [US2] Write `examples/input_demo.c` (name from `OVD-IDENTITY-001` §8.3): shows the last events (keys with modifiers, mouse at a cell, paste length and first line, focus, size), with options for mouse, paste, focus, and a deliberate crash (`--crash=abort` calls `abort`, `--crash=segv` raises `SIGSEGV` on POSIX and an access violation on Windows), quits on `q`; register it in `examples/CMakeLists.txt`
- [x] T122 [P] [US2] Extend `docs/validation/manual-checklist.md` with SC-006's steps and expected results: every key of spec US2 scenario 2 with modifiers, Ctrl+C as a key, mouse press, release, drag, wheel, a multi-line paste, focus out and in, resize while idle, closing the window, both `--crash` modes, `kill -TERM` (POSIX), Ctrl+C with input off (Windows: restored, then the process ends), with mouse off the terminal's own text selection still works, and the shell's state after each; record the Windows Terminal and POSIX terminal versions

### OVD-21 Document the implemented state [P] (depends on OVD-19)

- [x] T123 [P] Update `docs/architecture.md` and `README.md`: input and abnormal-exit restoration from planned to implemented, the process-wide handler exception (spec FR-036) and what is not covered (FR-034), with measured numbers only (Art. XV)
- [x] T124 [P] Update `docs/compatibility.md`: what the pty tests show per platform; Windows and real terminals as UNVERIFIED until the checklist is run

## Dependencies

```text
OVD-15 → OVD-16 → OVD-17 → OVD-18 → OVD-19 → { OVD-20, OVD-21 }
```

The chain is linear through OVD-19 because each step builds on the one
before and the platform files are shared: `platform.h` declares
functions both platform files must define, so splitting POSIX from
Windows would break the Windows cross build between merges. Parallel
work: T104 with T101–T103 inside OVD-15; OVD-20 with OVD-21.

## Parallel examples

```text
OVD-15: T101–T103 (headers, config) ‖ T104 (sanitize preset, CI)
OVD-20 (example, checklist)          ‖ OVD-21 (docs)
```

## Implementation strategy

MVP is OVD-15 through OVD-17: headless input proven by the decode table,
the fuzz test, and the allocation test (US1), with no terminal. OVD-18
makes it usable in a real terminal (US2), OVD-19 makes it safe to use
(US3); mouse, paste, and focus (US4, US5) ride along in the decoder and
the report sequences, so they need no phase of their own. SC-006 is the
owner's manual run: it does not block merging and is tracked in a
`needs-human` issue (Art. XVI).
