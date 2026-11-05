## Why

Feature 002's input and abnormal-exit restoration are on `main`. `README.md` and `docs/architecture.md` still call them specified but not implemented. `docs/compatibility.md` says nothing about the pty tests or what they show on each platform. T123 and T124 bring these documents up to date, with claims limited to what the merged tests show (Art. XV).

## What Changes

- **`README.md`.** Input and abnormal-exit restoration move to implemented. This covers:
  - the process-wide handler exception (FR-036);
  - what is not covered (FR-034), linked rather than restated;
  - `input.h` in the API list;
  - `input_demo` among the examples.
- **`docs/architecture.md`.**
  - The status line covers feature 002.
  - The layer diagram gains `input.h`, `input.c` and `decode.c`, with edges from their includes and calls.
  - A new input section covers the decoder's two layers, the wait, and allocation.
  - A new restoration section covers the process-wide record, the order of the handlers, and what is not covered.
  - Measurements gain the input numbers the tests print. These are counts, not times.
  - "Not in this architecture" drops input.
- **`docs/compatibility.md`.**
  - A new section says what the pty tests show on Linux and macOS, from the CI run on PR #64's reviewed head.
  - It says what Wine shows for input: only the headless tests.
  - Real terminals and Windows stay UNVERIFIED until the checklist runs (#65).
  - The platform table gains the sanitize jobs.
- **`specs/002-input-restore/tasks.md`.** T123 and T124 are ticked.

## Impact

Documentation only. No code or test changes.
