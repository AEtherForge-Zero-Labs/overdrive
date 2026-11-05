## 1. Example

- [x] 1.1 Write `examples/input_demo.c` (T121, design D1–D5) and register it in `examples/CMakeLists.txt`.
- [x] 1.2 Build it with `cmake --build --preset debug --target input_demo`, and smoke-run it on a pty: keys appear in the log, `q` exits 0, and `--crash=abort` dies by SIGABRT with the terminal restored.

## 2. Checklist

- [x] 2.1 Extend `docs/validation/manual-checklist.md` with checks I1 to I12 and a results template (T122, design D6).

## 3. Delivery evidence

- [x] 3.1 Run `debug`, `sanitize`, `debug-clang`, `shared`, `windows-cross-debug`, and gcc-14 `release` from clean with zero warnings.
- [x] 3.2 Tick T121 and T122, and reread the full diff.
