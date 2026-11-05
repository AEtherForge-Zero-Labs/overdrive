## Why

Feature 001 (render core) is implemented; its spec defers input and
abnormal-exit restoration to feature 002, because the first consumer
needs input and raw input makes an unrestored terminal unusable. Issue
#50 writes 002's Spec Kit feature directory so its implementation issues
can be cut.

## What Changes

- New `specs/002-input-restore/`:
  - `spec.md`: six user stories reduced to five after clarification
    (headless decoding, real-terminal keys, abnormal-exit restoration,
    mouse, paste and focus), FR-001–FR-040, SC-001–SC-007, and a
    Clarifications session with the owner's three answers;
  - `checklists/requirements.md`: the spec quality checklist;
  - `research.md`: decisions R101–R117 with provenance tags, and the
    tensions table;
  - `plan.md`: constitution check, public API, modules, the terminal-mode
    table, workstreams;
  - `data-model.md`, `quickstart.md`;
  - `contracts/include/overdrive/input.h` (new) and `terminal.h` (001's
    header plus the input and restoration config fields);
  - `tasks.md` and the cross-artifact analysis.
- No library code, public header, build file, or test changes.
  `include/overdrive/` is untouched: the contracts are proposals until
  an implementation issue copies them.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

Docs only. The proposed contracts shape a future public API (new
`input.h`, six new `OvdTerminalConfig` fields) and the process-wide
handler exception to 001 FR-002, so the PR is labeled `assumption` for
the owner's after-the-fact review; the owner already chose the three
clarified points. Implementation issues are created only after the owner
approves `speckit-taskstoissues`.
