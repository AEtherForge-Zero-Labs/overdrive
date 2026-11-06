## Why

`test_restore_pty` skips "SA_RESETHAND crash handler returns" wherever `sigaction` does not report `SA_RESETHAND` on read-back (R121; macOS). CI shows a test's output only when it fails, so whether Linux ran that case or skipped it was not recorded.

## What Changes

- **`tests/test_restore_pty.c`.** On Linux, the case fails instead of skipping when the probe says the flag is not reported. Other platforms still skip.
- **`docs/compatibility.md`.** It now records the result. The Linux jobs of CI run 42140940176 passed with the check in place: gcc-14 `debug`, `release`, `shared` and `sanitize`, and clang-19 `debug-clang`. So the case runs on Linux, and Linux reports the flag. This was ASSUMED in the plan and is now VERIFIED by that run.

With the requirement forced on macOS by a local edit, which was not committed, the case failed as intended.

## Impact

Tests and docs. The library is unchanged.
