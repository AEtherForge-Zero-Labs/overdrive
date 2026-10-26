# Tasks: 001-render-core

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [contracts/](contracts/include/overdrive/)

Format: `- [ ] T### [P] [Story] Description naming the file (depends on …)`.
`[P]` = parallel-safe with other `[P]` tasks in its phase (disjoint files).
Each phase maps to one GitHub issue (`OVD-NN`, see
[workgraph.json](workgraph.json)); once issues exist, lines gain `→ #<number>`.
Every behavior task is preceded by its test task, which must be seen failing.

## Phase 0 — OVD-01 Build skeleton, CI, public headers, core (blocks everything)

- [ ] T001 Create `CMakeLists.txt`: C23, extensions off, `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang), target `overdrive` + alias `overdrive::overdrive`, `C_VISIBILITY_PRESET hidden`, `OVERDRIVE_BUILDING`/`OVERDRIVE_SHARED` definitions, options `OVERDRIVE_BUILD_TESTS`, `OVERDRIVE_BUILD_EXAMPLES`, compiler-version check per baseline
- [ ] T002 Create `CMakePresets.json`: `debug`, `release`, `debug-clang`, `shared`, `windows-cross-debug` (MinGW + Wine emulator); test presets with `noTestsAction: error`
- [ ] T003 Copy the approved contract headers into `include/overdrive/` unchanged (`overdrive.h`, `core.h`, `terminal.h`, `render.h`) — contract freeze point
- [ ] T004 Create `tests/CMakeLists.txt` with `ovd_add_test(<name>)` helper (one line per test, append-only)
- [ ] T005 Write `tests/test_core.c`: result names for every enumerator and an out-of-range value; `ovd_version()` equals header macros
- [ ] T006 Implement `src/core.c`: `ovd_result_name`, `ovd_version`
- [ ] T007 Create `cmake/check_exports.cmake` and a `check_exports` CTest: fail on any exported symbol without `ovd_` (nm for ELF/Mach-O, objdump export table for PE) (FR-040)
- [ ] T008 Create `.github/workflows/ci.yml`: Linux GCC 14 debug+release, Linux Clang debug, macOS Apple Clang debug, MinGW cross + Wine; each runs the presets' configure/build/test; one `ci-ok` aggregate job
- [ ] T009 Add a header self-containment test: each public header compiles alone (`tests/test_headers.c` + generated per-header TUs in the build tree) and contains no OS include (FR-041)

## Phase 1 — Primitives (parallel lanes; each depends on OVD-01)

### OVD-02 Color [P]
- [ ] T010 [P] [US1] Write `tests/test_color.c`: RGB→256 cube and gray ramp, RGB→16, 256→16, indexed 0–15 passthrough, depth NONE drops colors; SGR encodings for every color kind and attribute; minimal-delta vs reset decisions
- [ ] T011 [P] [US1] Implement `src/color.h`/`src/color.c`: `ovd_internal` downsampling (R9) and SGR encoder writing into a caller buffer (bounded)

### OVD-03 Unicode [P]
- [ ] T012 [P] Vendor UCD `EastAsianWidth.txt` and `DerivedGeneralCategory.txt` for the pinned version into `third_party/ucd/` with the Unicode license
- [ ] T013 [P] Write `cmake/gen_unicode_tables.cmake` generating range tables into the build tree (Art. II); wire it from `CMakeLists.txt` (append-only block)
- [ ] T014 [P] [US1] Write `tests/test_unicode.c`: ASCII, CJK (width 2), combining marks, ZWJ emoji sequence, VS16, invalid/truncated UTF-8 → one U+FFFD per maximal subsequence, every C0/C1/DEL → U+FFFD, lone combining mark → U+FFFD, >15-byte grapheme → U+FFFD (whole grapheme, width 1), NUL, U+2028/9, bidi controls → U+FFFD, ZWJ sequence = one grapheme of its base's width, extenders after a U+FFFD attach to it (R15–R17)
- [ ] T015 [P] [US1] Implement `src/unicode.h`/`src/unicode.c`: next-grapheme iterator returning bytes, width, and replacement flag (R5, R6)

### OVD-04 Allocation and output buffer [P]
- [ ] T016 [P] Write `tests/test_alloc.c` (part 1): counting/failing test allocator in `tests/support/test_alloc.{c,h}`; output buffer is allocated once to a requested capacity, refuses appends past it without writing, resets for reuse; allocator dispatch rejects size 0 and overflow; resize helper allocates all new blocks before releasing old ones (R30–R32)
- [ ] T017 [P] Implement `src/alloc.h`/`src/alloc.c` (default and user allocator dispatch with overflow-checked sizes) and `src/output.h`/`src/output.c` (`OvdOutput` with fixed capacity per size, bounded append, reset; R30)

### OVD-05 Replay interpreter (test support) [P]
- [ ] T018 [P] Write `tests/support/vt_replay.{c,h}`: applies the R7 subset to a text screen model with styles; rejects any sequence outside the subset (so unexpected output fails tests); models autowrap on with deferred wrap and fails on printing in pending-wrap state or on any scroll (R33); `tests/support/screen.{c,h}` renders expected screens from text rows (Art. VI)
- [ ] T019 [P] Write `tests/test_vt_replay.c`: the interpreter's own tests (CUP, SGR deltas, wide glyphs, ED 2, unknown sequence → error)

### OVD-06 Platform layer [P]
- [ ] T020 [P] [US2] Write `src/platform/platform.h`: is-terminal, size, enter/leave (records prior modes in a struct), write-all, environment lookup — no OS types in the header
- [ ] T021 [P] [US2] Implement `src/platform/platform_posix.c` (termios ECHO only, on stdin only if it is a terminal (R27); TIOCGWINSZ; write loop with EINTR; enter/leave order and best-effort restore per R26, R28) — verify each call against POSIX docs and cite them in the PR
- [ ] T022 [P] [US2] Implement `src/platform/platform_windows.c` (console modes, code page 65001, `GetConsoleScreenBufferInfo`, `WriteFile` loop) — verify against Microsoft docs and cite them
- [ ] T023 [P] [US2] Implement color detection from the environment (R10) in platform-independent `src/detect.c` with `tests/test_detect.c` using an injected environment lookup; first match wins, NO_COLOR only when non-empty (R36)

## Phase 2 — Canvas (depends on OVD-03, OVD-04)

### OVD-07 Cell buffer and canvas
- [ ] T024 [US1] Write `tests/test_canvas.c`: fill, clip set/restore, text advance, clipping, wide glyph at right edge/clip, overwriting halves of a wide glyph incl. the other half outside the clip (R20), first half outside clip or left edge (R19), cell read-back, negative and zero rects, INT32_MIN/MAX coordinates (R18), closed canvas and nullptr (R22, R23), color normalization
- [ ] T025 [US1] Implement `src/buffer.h`/`src/buffer.c` (`OvdBuffer` grid, resize via allocator, wide-glyph pairing invariant)
- [ ] T026 [US1] Implement `src/canvas.c` (`ovd_canvas_size`, `_clip`, `_set_clip`, `_fill`, `_text`, `_cell`)

## Phase 3 — Rendering (parallel pair)

### OVD-08 Differential renderer (depends on OVD-02, OVD-04, OVD-05, OVD-07)
- [ ] T027 [US1] Write `tests/test_diff.c`: replay equivalence (SC-001) over a corpus of frames incl. random frames from a fixed seed; unchanged frame → 0 bytes (SC-002); one-cell change ≤ 64 bytes (SC-003); full redraw after unknown screen; bottom-right cell; depth 16/256/NONE output; adversarial frames (alternating styles, all-RGB, max-length graphemes) never exceed the output bound (R30); output framing (R34)
- [ ] T028 [US1] Implement `src/diff.h`/`src/diff.c` (R8)

### OVD-09 Boxes [P] (depends on OVD-07)
- [ ] T029 [P] [US3] Write `tests/test_box.c`: five border styles, clip, small rects draw nothing, interior untouched
- [ ] T030 [P] [US3] Implement `src/box.c` (`ovd_canvas_box`)

## Phase 4 — Terminal and frames

### OVD-10 Headless terminal and frame API (depends on OVD-04, OVD-07, OVD-08)
- [ ] T031 [US1] Write `tests/test_terminal.c`: config defaults; create/destroy headless; frame state machine errors; set_size + resize full redraw; invalidate; sink failure → `OVD_ERROR_IO` then full redraw; destroy with open frame; two terminals at once (FR-002); allocation failure at create and at resize leaves state intact (R21, R31); sink re-entrancy → OVD_ERROR_STATE (R24); headless ignores the environment (R35); set_size and create argument validation (R22)
- [ ] T032 [US1] Implement `src/terminal.c` and `src/internal.h` (headless backend, frame begin/end/abandon, one write per frame, optional sync markers)

### OVD-11 System backend (depends on OVD-06, OVD-10)
- [ ] T033 [US2] Implement the system backend in `src/terminal_system.c` via `platform.h` (called from `src/terminal.c` through `src/internal.h`): enter/leave order (FR-011), size re-read per frame (FR-014), `OVD_ERROR_NOT_A_TERMINAL` with nothing written, rollback on failure, best-effort restore, exit sequence per R26
- [ ] T034 [US2] Add a non-interactive test: stdout redirected → `OVD_ERROR_NOT_A_TERMINAL`, output file empty (`tests/test_system_notty.c`)

### OVD-13 Benchmark and steady-state allocations [P] (depends on OVD-10)
- [ ] T035 [P] [US4] Write `tests/test_alloc.c` (part 2) → separate file `tests/test_steady_state.c`: 100 frames after the first make 0 allocator calls (SC-004)
- [ ] T036 [P] [US4] Write `bench/render_benchmark.c` + option `OVERDRIVE_BUILD_BENCHMARKS` + `bench` preset; prints scenarios, bytes/frame, timing, machine/compiler/build type (SC-003, SC-008)

## Phase 5 — Demonstration and documentation

### OVD-12 hello_overdrive (depends on OVD-09, OVD-11)
- [ ] T037 [US2] Write `examples/hello_overdrive.c`: brand palette screen, tagline, boxes, a few seconds of frames with a changing counter, clean exit
- [ ] T038 [US2] Write `docs/validation/manual-checklist.md`: steps and expected results for SC-007; record Windows Terminal and POSIX results in the PR

### OVD-14 Documentation of implemented state (depends on OVD-11, OVD-13)
- [ ] T039 Update `docs/architecture.md` and `README.md` from planned to implemented, with measured numbers only (Art. XV)
- [ ] T040 Write `docs/compatibility.md`: terminals actually tested, with versions

## Dependencies

See [`docs/plan/workstreams.md`](../../docs/plan/workstreams.md). Critical
path: OVD-01 → OVD-03 → OVD-07 → OVD-08 → OVD-10 → OVD-11 → OVD-12.
