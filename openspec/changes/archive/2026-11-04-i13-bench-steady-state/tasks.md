## 1. Tests first

- [x] 1.1 Write `tests/test_steady_state.c` (T035), register it in `tests/CMakeLists.txt`, and watch it fail against a scratch mutation that makes every `ovd_frame_begin` reallocate; revert the mutation and watch it pass.

## 2. Benchmark

- [x] 2.1 Write `bench/render_benchmark.c` and `bench/CMakeLists.txt` (T036), append `OVERDRIVE_BUILD_BENCHMARKS` and `add_subdirectory(bench)` to `CMakeLists.txt`, and add the `bench` configure and build presets to `CMakePresets.json`.
- [x] 2.2 Run the benchmark from `cmake --build --preset bench`; check the falsifiers: frame count argument 0, negative, non-numeric, and 1; confirm SC-003's worst case prints within 64 bytes and the no-change scenario prints 0 bytes.

## 3. Delivery evidence

- [x] 3.1 Run all six test presets from clean builds with zero warnings, plus `cmake --build --preset bench`; `ctest --preset debug -R test_steady_state` passes.
- [x] 3.2 Record R47 in `research.md`, with the first measurement and its machine, and tick T035 and T036 in `tasks.md`.
- [x] 3.3 Reread the full diff and post the plan comment on #13; the PR and its evidence are published by `tx:close`.
