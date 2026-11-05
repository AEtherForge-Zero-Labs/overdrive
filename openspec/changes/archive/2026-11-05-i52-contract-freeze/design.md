## Context

002's plan puts the contract freeze and the sanitizer preset first
(plan.md, Workstreams), because every later issue builds on both. This
change copies the headers verbatim and changes no behavior that already
exists: every new field's default reproduces 001's behavior.

## Decisions

### D1. Order of create's checks

The new checks are argument checks. They run with the existing ones,
before the system backend acquires anything, so a rejected config writes
nothing and allocates nothing (001's `rejects` helper checks both). The
interim `OVD_ERROR_UNSUPPORTED` for `input` runs after every argument
check:
- an invalid config is still `OVD_ERROR_INVALID_ARGUMENT`, as it will be
  once input exists;
- a valid config with input on is `OVD_ERROR_UNSUPPORTED`.

This holds on both backends, because neither can read input yet.

### D2. The sanitize preset

The preset inherits `debug` and sets these flags through cache
variables, never on the command line (AGENTS.md, Build):
- `CMAKE_C_FLAGS`: `-fsanitize=address,undefined
  -fno-sanitize-recover=all -fno-omit-frame-pointer`;
- the same `-fsanitize` set on the executable and shared linker flags.

`-fno-sanitize-recover=all` makes any UBSan report fail the test, so a
report cannot pass silently. The test preset inherits `test-base`.

Choosing the CI rows:
- **Linux:** GCC 14, the baseline's GCC minimum.
- **macOS:** Apple Clang (`cc`), the existing macOS compiler. Homebrew
  GCC on macOS does not ship ASan for arm64, so it is not declared there.

### D3. The baseline amendment

A new "Sanitizers" section names, per platform, the compiler whose
runtime the preset uses, and notes that the declared path does not cover
Windows. Plus a change log line.

## Risks

- Existing code may show a sanitizer report. T104 makes that a separate
  bug issue, and this one halts until that issue merges. Running the
  suite under the preset is the first step after adding it.
- AddressSanitizer adds global symbols to the static archive, which the
  export check (R12) could read as non-`ovd_` exports.
  - **Observed** on Apple Clang 21: `check_exports` failed with
    `Non-ovd export: asan_globals_registered`, and nothing else did.
  - The sanitize test preset therefore excludes exactly `check_exports`.
  - The two fixture tests still run under it and pass.
  - Every other preset runs the full check.
  - JSON has no comments, so the reason is recorded in the baseline's
    Sanitizers section.
- **Observed:** the existing suite, run under the preset before any other
  change, passed 18/18 with no report. A scratch out-of-bounds read
  added to `test_headers`, then reverted, made the test fail with
  `heap-buffer-overflow`, so a report does fail a test.
