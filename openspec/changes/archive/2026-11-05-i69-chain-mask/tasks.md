## 1. Test first

- [x] 1.1 Add "application handler's mask" (SIGINT, SIGABRT, SIGINT with SA_NODEFER) to `tests/test_restore_pty.c`, and watch all three fail on `main`'s handler: the handler reports a wrong mask.

## 2. Fix

- [x] 2.1 In `call_previous`, set the mask from `uc_sigmask`, the application's `sa_mask`, and the signal unless SA_NODEFER or SA_RESETHAND is set. Watch the three cases pass, and every other restore case still pass.
- [x] 2.2 Add the R121 bullet in `specs/002-input-restore/research.md`.

## 3. Delivery evidence

- [x] 3.1 Run `debug`, `sanitize`, `debug-clang`, `shared`, `windows-cross-debug`, and gcc-14 `release` from clean with zero warnings.
