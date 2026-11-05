## Context

See proposal.md for why. What exists on `main` decides what the documents
may say:
- the public headers `core.h`, `terminal.h`, `render.h`, and
  `overdrive.h`;
- the library sources in `src/`;
- the test suite (CTest);
- the presets;
- the R47 benchmark record.

Art. XV requires the following:
- performance claims cite the benchmark, the command, the machine, the
  build type, and the result;
- compatibility claims name the terminal, its version, and the platform;
- the identity §7.2 phrases are not used unless the specific claim is
  established.

## Goals / Non-Goals

**Goals:**
- Every statement in the three files is true of `main`, or is labeled as
  not implemented.
- Each number is copied from R47 with its provenance, never re-measured
  or rounded into a different claim.
- `docs/compatibility.md` separates three things: terminals observed,
  none so far; automated platforms; and what each automated platform
  proves.

**Non-Goals:**
- Running real-terminal checks. Art. XVI reserves real-terminal
  observations for a human. The manual checklist is OVD-12 (#14), which
  is still open, so this change does not link a file that does not exist
  yet.
- New measurements. R47 is the only benchmark record, and repeating it
  here would create a second, unrecorded number.
- Documenting unmerged work: `hello_overdrive` (#14) and the pty test
  (#45).

## Decisions

- **The architecture diagram follows the `#include` graph, not intent.**
  The graph, read from `src/`:
  - `terminal.c` uses `internal.h` (alloc, buffer, output, platform) and
    `diff`;
  - `terminal_system.c` uses `platform` and `detect`;
  - `canvas.c` uses `buffer` and `unicode`;
  - `box.c` uses only the public canvas API;
  - `buffer` uses `alloc` and `unicode`;
  - `diff` uses `buffer`, `output`, and `color`;
  - `output` uses `alloc`.

  *Rejected:* keeping the planned diagram, which omits
  `terminal_system.c` and wires `box.c` and `detect.c` wrongly.
- **The issue comment's four corrections are applied with the wording it
  suggests,** citing R23, R30, R31, and R33.
- **Measurements appear once, in `docs/architecture.md`,** as R47's table
  with its provenance. The README links to them rather than copying the
  table, so the two cannot drift.
- **Compatibility, the narrowest honest reading of T040 ("terminals
  actually tested, with versions").** No agent has observed a terminal
  emulator, so the terminal table states that none has been observed.
  The CI platforms are listed separately, with what each one proves (the
  headless suite, and the stdout-redirected system path), and with the
  versions logged in the CI run of PR #45 (run 41974704883,
  2026-11-05). Recorded on the issue as an assumption.
- **README usage reflects the build as it is.** There are no install
  rules, so consumers use `add_subdirectory` and the
  `overdrive::overdrive` alias target. The README says so directly
  instead of showing `find_package`.

## Risks / Trade-offs

- [The CI versions go stale as runner images move] → The table is
  dated, names its run, and says `macos-latest` moves (baseline.md).
- [Docs drift as #14 and #45 merge] → Their own PRs update these files
  if their claims change them. This change describes only `main`.
