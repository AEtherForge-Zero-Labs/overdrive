# Quickstart: validating Input and Abnormal-Exit Restoration

How to show 002 works, once its issues are merged. Each step names the
spec item it proves. Contracts: [input.h](contracts/include/overdrive/input.h),
[terminal.h](contracts/include/overdrive/terminal.h).

## Prerequisites

The toolchain in `.specify/memory/baseline.md`; builds only through the
presets (`AGENTS.md`, Build).

## 1. Headless suite (US1, US4, US5; SC-001, SC-004, SC-005)

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -R 'decode|input|steady_state'
```

Expected: `test_decode` passes every table row (SC-001);
`test_input` passes the wait, escape-delay, paste-chunk, and resize
cases; `test_steady_state` reports 0 allocator calls in `ovd_input_wait` and
`ovd_input_feed` (SC-004).

## 2. Fuzz under sanitizers (SC-002)

```bash
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize -R decode_fuzz
```

Expected: 10⁷ bytes decoded, no sanitizer report, every invariant check
passing.

## 3. Pseudo-terminal restoration (US2, US3; SC-003), Linux and macOS

```bash
ctest --preset debug -R 'system_pty|restore_pty'
```

Expected: raw-mode flags set during the run and restored after destroy;
for each signal in spec FR-030, the termios after the child dies equals
the termios before create, and the child's `waitpid` status equals the
control child's; a signal that interrupts a frame write still leaves
the exit sequence decodable (its CAN prefix, research R112); an
application handler installed after create survives destroy.

## 4. Manual checklist (SC-006)

Run `input_demo` in Windows Terminal and one POSIX terminal following
`docs/validation/manual-checklist.md`, recording both versions: every
key of US2 scenario 2, mouse (press, release, drag, wheel), a
multi-line paste, focus out and in, a resize while idle, Ctrl+C as a
key, closing the window, and the demo's crash option. Until a human
runs it, it is UNVERIFIED in every PR (CLAUDE.md, Repo specifics).

## 5. Unchanged guarantees (SC-007)

The existing CI matrix (`.github/workflows/ci.yml`) and the export check
pass, with 0 warnings and 0 non-`ovd_` symbols.
