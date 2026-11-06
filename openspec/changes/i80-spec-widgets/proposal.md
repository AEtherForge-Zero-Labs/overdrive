## Why

The first consumer, the owner's console task manager, needs menus, prompts, lists, and tables. `specs/003-packaging/spec.md` names them as a later feature driven by that consumer. Today it has to build each one from `ovd_canvas_text`, `ovd_canvas_box`, and its own key handling: selection, scrolling, cutting text of mixed widths, and editing text one user-visible character at a time. The owner chose immediate-mode widgets with caller-owned state (2026-11-06), to take that work off every consumer.

## What Changes

- **New:** the Spec Kit feature directory `specs/004-widgets/`:
  - `spec.md`, with its quality checklist: three user stories (list, prompt, table), FR-001 to FR-023, and SC-001 to SC-006;
  - `plan.md`, `research.md` (R301–R321), `data-model.md`, and `quickstart.md`;
  - `contracts/include/overdrive/widgets.h`, the proposed public header. It compiles under `-std=c23 -Wall -Wextra -Wpedantic -Werror`;
  - `tasks.md` and `workgraph.json`: six issues, OVD-25 to OVD-30.
- Planning amended the spec twice, and recorded both in `research.md`:
  - an "unchanged" outcome (R304);
  - in-place repair of text written into the prompt's buffer directly (R312).

## Capabilities

### New Capabilities

None in OpenSpec. The requirements live in the Spec Kit directory (constitution v1.4.0: Spec Kit keeps requirements, OpenSpec records each transaction). Hence `skip_specs: true`.

### Modified Capabilities

None.

## Impact

Documentation only. No library code, build file, or public header changes in this transaction. The proposed header lives under `specs/004-widgets/contracts/` until OVD-25 freezes it into `include/overdrive/`. The implementation issues are created by `speckit-taskstoissues` after this merges, with the owner's approval.
