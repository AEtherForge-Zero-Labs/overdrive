## Why

The independent review of the change `2026-11-05-i69-chain-mask` passed it with three notes:

- **Delivery order.** `call_previous` set the mask at delivery first, then blocked the additions. Between the two calls, the signal itself and the application's `sa_mask` were unblocked. XSH `pthread_sigmask` delivers a pending unblocked signal "before pthread_sigmask() returns", so a copy that became pending during the restore ran before the application's handler, not after it.
- **Coverage.** No case covered the mask at delivery. A mutant that set the mask to empty instead of `uc_sigmask` passed every case.
- **Wording.** A sentence in R121 had two parentheticals in a row and ran past the wrap width.

## What Changes

- **`src/platform/platform_posix.c`.** `call_previous` reaches the same target mask without unblocking anything the target blocks. It first blocks the additions, then unblocks the handled signals that are in neither `uc_sigmask` nor the additions. This supersedes the order in the archived change.
- **`tests/test_restore_pty.c`.** The "application handler's mask" subject blocks `SIGHUP` before it sends the signal. Its handler requires `SIGHUP` to be still blocked.
- **`specs/002-input-restore/research.md`.** R121 states the order and the reason for it, and the sentence is reworded.

## Impact

The public API is unchanged, and Windows is untouched.
