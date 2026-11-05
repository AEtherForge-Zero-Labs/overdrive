## Why

Feature 002's US3 (spec FR-030 to FR-036, SC-003): a system terminal must be restored when the process ends through a catchable termination or crash signal, or on Windows a console control event, an unhandled exception, or `abort`, without the consumer reaching `ovd_terminal_destroy`. This is OVD-19 (#56), tasks T117 to T120.

While planning, a check of Microsoft's UCRT `signal.cpp` showed that R113's claim rule is wrong. That rule relied on the C runtime's `SIGINT` and `SIGBREAK` dispositions read at create. Probing them with `signal()` registers the runtime's console handler ahead of Overdrive's in registration, so Overdrive's would run first. The owner chose option B on #56: register Overdrive's handler first, then have the runtime register after it, and document the limit.

## What Changes

- POSIX (`platform_posix.c`, `platform.h`): one process-wide restoration record and an `IDLE → ARMED → CLAIMED → DONE` state machine (R112). It has:
  - a handler for each FR-030 signal not ignored before create, which chains by the previous action's flags, then resets to `SIG_DFL` and raises;
  - the handled signals blocked in the creating and destroying thread;
  - at destroy, each previous action put back only where the slot is still Overdrive's.
- Windows (`platform_windows.c`): the same state machine, plus:
  - a console control handler for close, Ctrl+C and Ctrl+Break, registered first;
  - a `signal()` round trip for `SIGINT` and `SIGBREAK`, so the C runtime registers its handler after Overdrive's and claims events the application handles (option B);
  - a chained unhandled-exception filter;
  - a CRT `SIGABRT` handler.
- Wiring (`terminal_system.c`, `internal.h`):
  - create reserves the record before its first mode change and arms it last;
  - a failed create releases it;
  - destroy claims, restores once, and puts the handlers back;
  - a second system terminal registers nothing;
  - `restore_on_exit` false installs nothing.
- Tests: `tests/test_restore_pty.c` (T117) and its CMake registration.
- Docs:
  - research R113 rewritten for option B, and R121 for the decisions made while implementing;
  - spec FR-034 gains option B's limit;
  - data-model's restoration record;
  - `terminal.h`'s `restore_on_exit` comment gains the same limit (owner decision on #56);
  - the plan.md mode table;
  - T117 to T120 ticked.

## Impact

- No public symbol is added or removed. The `restore_on_exit` documentation changes as described above.
- Process-wide handlers are now installed when `restore_on_exit` is on (the default), as FR-035 and FR-036 specify.
- Windows behavior is compiled and cross-built but not run; real terminals are UNVERIFIED until OVD-20's checklist.
