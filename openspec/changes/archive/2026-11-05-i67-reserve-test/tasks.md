## 1. Test

- [x] 1.1 Add "SIGTERM during create" to `tests/test_restore_pty.c`. Watch it fail with the blocking removed from `ovd_platform_restore_reserve` (a local mutant, not committed), then pass without the mutant.

## 2. Delivery evidence

- [x] 2.1 Run `debug`, `sanitize`, `debug-clang`, `shared`, `windows-cross-debug`, and gcc-14 `release` from clean with zero warnings.
