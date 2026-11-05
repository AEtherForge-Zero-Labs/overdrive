## Context

- R112 and R113 specify the restoration record and its state machine. data-model.md defines the record's fields and invariants.
- OVD-18 left the system backend with:
  - raw input (step 4b);
  - a process-wide SIGWINCH wakeup (step 4c);
  - one exit sequence built in `terminal_system.c` (CAN, SGR 0, the reports off in reverse, autowrap on, cursor shown, leave the alternate screen or move to the last row).
- `terminal.c` calls `ovd_terminal_system_open` before its first mode change and `ovd_terminal_system_enter` as create's last step. It calls `ovd_terminal_system_undo` on every create failure after open, and `ovd_terminal_system_close` at destroy. So the whole create and destroy window is reachable from `terminal_system.c` without touching `terminal.c`.

## Goals / Non-Goals

Goals:
- R112 on POSIX and R113, as amended by option B, on Windows.
- Restoration at most once (FR-033).
- The application's handlers keep running (FR-032).
- `restore_on_exit` false installs nothing (FR-035).
- T117's pty test as the per-platform proof (SC-003).

Non-goals:
- Restoring after a normal exit without destroy (FR-034).
- Updating the record's exit sequence on every frame (D5).
- Signals delivered to another thread during create or destroy (FR-034).

## Decisions

### D1. One record per process, owned by the first armed system terminal

The record lives in static storage in each platform file. It holds:
- `state`, an `atomic_int`: `IDLE`, `ARMED`, `CLAIMED`, `DONE`;
- a separate `atomic_bool owned`;
- the exit bytes (`OVD_PLATFORM_EXIT_MAX` = 96, CAN first);
- the modes to restore: on POSIX the termios read before echo, when step 4 ran; on Windows the output mode, code page and input mode with their steps;
- the previous handlers.

`static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_BOOL_LOCK_FREE == 2)`. C23 7.14.1.1 lets a handler use lock-free atomic objects.

`owned` is taken with `atomic_exchange` at open, before anything changes. A second system terminal finds it taken and registers nothing (R112 step 1, 001 R29). The state machine is unchanged by this.

Rejected: using a `FILLING` state for ownership. A handler finding a state other than `IDLE` that will never reach `DONE` could spin forever.

### D2. Platform interface (`platform.h`)

```c
#define OVD_PLATFORM_EXIT_MAX ((size_t)96)
[[nodiscard]] OvdResult ovd_platform_restore_reserve(bool *owner);
void ovd_platform_restore_arm(const OvdPlatformModes *modes, const char *exit, size_t length);
void ovd_platform_restore_release(void);
[[nodiscard]] bool ovd_platform_restore_claim(void);
void ovd_platform_restore_disarm(bool claimed);
```

- `reserve`: takes ownership if free. On POSIX the owner then blocks the handled signals in the calling thread (`pthread_sigmask(SIG_BLOCK)`, saving the old mask in the record). `OVD_ERROR_PLATFORM` if the mask cannot be set.
- `arm`: owner only, as create's last step. Fills the record, stores `ARMED` (release), installs the handlers, then unblocks (POSIX).
- `release`: create failed before arming. Unblocks and gives up ownership.
- `claim`: destroy's first step. Blocks (POSIX), then compare-exchanges `ARMED` → `CLAIMED`. Returns true if it won. If a handler holds `CLAIMED`, it waits for `DONE` and returns false.
- `disarm(claimed)`: stores `DONE` if claimed, puts back each handler whose slot is still Overdrive's, stores `IDLE`, unblocks, and gives up ownership.

### D3. POSIX handler (R112)

The handled set is `SIGHUP SIGINT SIGQUIT SIGTERM SIGABRT SIGSEGV SIGBUS SIGFPE SIGILL`.

At arm, for each signal: the previous action is read. One that is already Overdrive's is treated as `SIG_DFL`, as `wake_open` does. A signal whose previous action is `SIG_IGN` gets no handler.

The installed action:
- `sa_sigaction = on_fatal`;
- `sa_flags = SA_SIGINFO | (previous & (SA_RESTART | SA_ONSTACK))`, so an application's restart and alternate-stack choices hold while Overdrive's handler is in place;
- `sa_mask` = the previous mask plus every handled signal.

`on_fatal(signal, info, context)` saves errno and does an acquire load of `state`, then:
1. If the signal is a termination signal and the previous action is a handler (`sa_sigaction` with `SA_SIGINFO`, else an `sa_handler` other than DFL/IGN): call it and return.
2. Otherwise, restore once: compare-exchange `ARMED` → `CLAIMED`. On a win: `write` the exit bytes (looping on partial writes and EINTR), `tcsetattr(TCSANOW)` the saved termios when step 4 ran, then store `DONE`. If the state was `CLAIMED`, spin until it changes. If `DONE` or `IDLE`, skip.
3. Call the previous handler if there is one.
4. Set `SIG_DFL` with `sigaction` and `raise` the signal. It is blocked while the handler runs, so it stays pending and is delivered under the default action when the handler returns.

Every call is on POSIX.1-2024 XSH 2.4.3's async-signal-safe list: `write`, `tcsetattr`, `sigaction`, `raise`, and the `atomic_*` functions with an atomic argument. That list was VERIFIED from the fetched 2.4 text. The code cites it.

`pthread_sigmask` is quoted in the code:
- "If any of the SIGFPE, SIGILL, SIGSEGV, or SIGBUS signals are generated while they are blocked, the result is undefined, unless the signal was generated by ... kill(), pthread_kill(), raise(), or sigqueue()" (FR-034's not-covered fault in create or destroy);
- "at least one of those signals shall be delivered before the call to pthread_sigmask() returns".

### D4. Windows (R113 amended by option B, owner decision on #56)

`arm`:
- fills the record and stores `ARMED`;
- registers `on_console` with `SetConsoleCtrlHandler(on_console, TRUE)`;
- then, for `SIGINT` and `SIGBREAK`, runs `previous = signal(s, SIG_DFL)` and, unless that returned `SIG_ERR`, `signal(s, previous)`.

The round trip changes no disposition, but its first call makes the C runtime register its console handler. VERIFIED in Microsoft's UCRT `misc/signal.cpp`, ReactOS mirror, lines 312–323. It then runs before Overdrive's, since handlers run last-registered first (ASSUMED [MS-CTRL]).

The runtime's `ctrlevent_capture` (same file, lines 163–211, VERIFIED):
- returns FALSE for `SIG_DFL`, so Overdrive's handler runs and restores;
- returns TRUE without calling anything for `SIG_IGN`;
- for an application handler, resets the action to `SIG_DFL`, calls the handler, and returns TRUE, so nothing is restored. That is FR-032's claim rule, enforced by the runtime itself.
- It maps every event other than `CTRL_C_EVENT` to `SIGBREAK`, so an application `SIGBREAK` handler also claims the close event.

Limits, documented in FR-034, R113 and `terminal.h`:
- When `signal()` for `SIGINT` or `SIGBREAK` ran before create, the runtime's handler was registered earlier and runs after Overdrive's. Ctrl+C and Ctrl+Break then restore even when the application handles them.
- An application `SIGBREAK` handler claims the close event.
- The round trip briefly sets `SIG_DFL`: a Ctrl+C arriving in that instant is not claimed.
- The CRT linked by MinGW (msvcrt.dll) was not checked (UNVERIFIED).

`on_console(type)`: for `CTRL_C_EVENT`, `CTRL_BREAK_EVENT` and `CTRL_CLOSE_EVENT`, restore once, then return FALSE.

`abort`: `signal(SIGABRT, on_abort)`. A previous `SIG_IGN` is put back and nothing is installed. `on_abort` restores once and calls a previous handler that is a function.

Exceptions: `SetUnhandledExceptionFilter(on_exception)`, saving the previous filter. `on_exception` restores once, then returns the previous filter's result, or `EXCEPTION_CONTINUE_SEARCH` without one.

Restore once uses R112's compare-exchange:
- write the exit bytes (`WriteFile` loop);
- then, in destroy's order, the input mode (when the echo step ran), the code page, and the output mode.

`claim` and `disarm` work as on POSIX, without a mask. `disarm`:
- removes the console handler;
- puts back `SIGABRT`'s previous handler, unless the one it replaced was not Overdrive's, in which case that one is put back again;
- does the same for the exception filter (R113's second call).

### D5. Wiring (`terminal_system.c`, `internal.h`)

`OvdTerminalSystem` gains `bool restore_owner`.
- **open:** after step 1 (stdout is a terminal), and with `config->restore_on_exit`, calls `reserve`. Every later failure in open releases.
- **enter:** after its write succeeds, the owner calls `arm` with the exit sequence built for the create-time rows.
- **undo:** leaves, then releases if owner.
- **close:**
  - the owner calls `claim`;
  - the exit sequence is written unless a handler already restored;
  - then leave;
  - the owner then calls `disarm(claimed)`;
  - a non-owner writes and leaves as before.

The record's exit sequence is fixed at create. Without the alternate screen it moves the cursor to the last row at create's size. If the terminal has since been resized, the cursor may land on a different row than destroy would pick. This is the narrowest reading of T120's "the exit sequence built once into the record", recorded in R121.

### D6. Tests (`tests/test_restore_pty.c`, T117)

The pty and children:
- Each case runs on a fresh pty.
- Each child is forked. It calls `setsid`, opens the slave without `O_NOCTTY`, calls `ioctl(TIOCSCTTY)`, and puts the slave on fds 0 and 1. The control-terminal setup is INFERRED and checked by a control child that dies by `SIGINT` when `0x03` is written in canonical mode.
- The children set `RLIMIT_CORE` to 0. Core flags are therefore equal but not exercised (UNVERIFIED).
- A pipe carries readiness and results back to the parent.
- The parent drains the master, then compares the slave's termios with the one before create, the exit bytes, and the `waitpid` status against a control child that does the same without Overdrive.

Cases are as listed in T117:
- every FR-030 signal, sent with `kill`;
- a real fault, a write to a `PROT_NONE` page, so UBSan sees nothing to report;
- the Ctrl+C, Ctrl+\ and Ctrl+Z keys;
- a frame write interrupted while blocked on a full pty;
- the application handler cases;
- `SIGHUP` ignored;
- `restore_on_exit` false;
- two system terminals;
- a crash after destroy;
- an application `SIGSEGV` handler;
- a seeded create and destroy loop under `SIGTERM`, checking that enter and exit sequences pair one to one.

Registered POSIX-only, with `ASAN_OPTIONS=handle_segv=0:handle_sigbus=0:handle_sigfpe=0:handle_sigill=0:handle_abort=0`, so the `sanitize` preset tests Overdrive's handlers, not the sanitizer's.

### D7. Docs

- research.md: R113 rewritten for option B, with the UCRT citations; new R121 for D1, D3's flag inheritance, and D5.
- spec FR-034: option B's limits.
- data-model.md:
  - the record gains `owned`;
  - under option B no `SIGINT`/`SIGBREAK` disposition is saved: carried item 2 is answered by saying so;
  - the Windows fields are listed.
- `terminal.h`: the `restore_on_exit` comment's not-covered list gains option B's limits.
- plan.md: the handler row of the mode table matches the code.
- tasks.md: T117–T120 ticked.

## Risks / Trade-offs

- Calling an application's crash handler and then raising kills the process even if that handler meant to recover by returning. R112 specifies this. A handler that leaves with `longjmp` is not affected.
- The test depends on pty and session behavior on Linux and macOS. CI proves Linux.
- Windows is cross-built only. The console handler, exception filter and `abort` paths are not run (UNVERIFIED until OVD-20's checklist).
