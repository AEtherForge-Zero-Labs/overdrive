## Why

On POSIX, Overdrive's handler chained to the application's previous handler with Overdrive's own signal mask still in effect. That mask blocks all nine handled signals. Without Overdrive, the application's handler runs with only its own `sa_mask`, plus the signal unless `SA_NODEFER` or `SA_RESETHAND` is set (XSH `sigaction`). FR-031 and FR-032 ask for the process to behave as without Overdrive.

What the difference allowed:
- a long application `SIGINT` handler could not be ended by `SIGTERM`;
- a handler that left by `longjmp` left all nine signals blocked.

## What Changes

- **`src/platform/platform_posix.c`.** Before calling the application's handler, `call_previous` sets the thread's mask in two `pthread_sigmask` calls: to `uc_sigmask` of the interrupted context, then adding the application's `sa_mask` and, unless `SA_NODEFER` or `SA_RESETHAND` is set, the signal. This replaces the separate `SA_NODEFER` unblock. Overdrive's own restore still runs under R112's mask. Without a context, the mask is left as it is.
- **`tests/test_restore_pty.c`.** It gains "application handler's mask" for `SIGINT` (the termination path), `SIGABRT` (the crash path), and `SIGINT` with `SA_NODEFER`. The application's handler, installed before create with `SIGUSR2` in its `sa_mask`, checks its own mask: `SIGUSR2` blocked, `SIGTERM` and `SIGQUIT` not, and the signal blocked unless `SA_NODEFER`. All three failed before the change, and pass after it.
- **`specs/002-input-restore/research.md`.** R121 gains a bullet for the mask.

Standard text used, all from POSIX.1-2024:
- XSH `sigaction`:
  - on the mask installed for a handler: "the union of the current signal mask and the value of the sa_mask", then the signal "unless SA_NODEFER or SA_RESETHAND is set";
  - on the third argument: it "can be cast to a pointer to an object of type ucontext_t" referring to "the receiving thread's context that was interrupted";
  - on return: "the original signal mask is restored".
- XSH 2.4.3 lists `pthread_sigmask`, `sigaddset` and `sigaction`.
- `<signal.h>` defines `ucontext_t` with `uc_sigmask`.

## Impact

POSIX restoration only; Windows is untouched. The public API is unchanged.
