## 1. Test first

- [x] 1.1 Turn `OVERDRIVE_BUILD_EXAMPLES` on in the base preset, add `examples/CMakeLists.txt` and a stub `examples/hello_overdrive.c`, and append `hello_overdrive_notty` to `tests/CMakeLists.txt`. Watch the test fail against the stub, which exits 0 and prints nothing.

## 2. Example

- [x] 2.1 Write `examples/hello_overdrive.c` (T037) per design.md: argument parsing, Ctrl+C flag, default config, the paced frame loop, the layout, error reporting after restore, and the summary line. `hello_overdrive_notty` passes.
- [x] 2.2 Check the falsifiers: the arguments `0`, `61`, `-1`, `+5`, `5x`, `""`, and two numbers each print usage and exit 1; `1` and `60` are accepted.
- [x] 2.3 Run the example in a scratch pseudo-terminal at 80×24, then at 1×1 and at 200×60, with a resize mid-run and with Ctrl+C mid-run. Replay the captured output through the test replay interpreter and confirm: every byte is accepted, the last frame shows the layout and the new size, the exit sequence follows, and echo is on again afterwards.
- [x] 2.4 Cover the real-terminal checks that earlier decisions defer to this checklist (research §E's "Unverified" list, R38, R40, and the comments in `src/terminal_system.c` and `src/platform/platform_windows.c`): add `--synchronized-output` and `--no-alternate-screen`, show both settings in the Frame panel, and draw R38's ZWJ probe row. Replay both options and the probe through the scratch pseudo-terminal check.

## 3. Checklist

- [x] 3.1 Write `docs/validation/manual-checklist.md` (T038): setup and builds for both platforms, the SC-007 steps with expected results, the three checks from the issue's comment, the deferred checks of task 2.4, recovery commands, and a results template that records versions.

## 4. Delivery evidence

- [x] 4.1 Run all six test presets from clean builds with zero warnings; `cmake --build --preset debug --target hello_overdrive` succeeds; the docs-form check passes on the new document.
- [x] 4.2 Record R48 in `research.md` and tick T037 and T038 in `tasks.md`.
- [x] 4.3 Reread the full diff and post the plan comment on #14; the PR and its evidence are published by `tx:close`.
- [x] 4.4 Make the example build with the CI cross toolchain, whose `<time.h>` lacks `timespec_get` and `TIME_UTC` (msvcrt): fall back to `clock()` there. Reproduce the failure locally with msvcrt headers, then confirm both runtimes compile clean, the suite passes, and a scratch build that forces the fallback paces correctly in the pseudo-terminal check.
