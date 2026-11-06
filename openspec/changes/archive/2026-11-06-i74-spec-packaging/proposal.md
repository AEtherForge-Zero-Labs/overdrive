## Why

The owner named the first consumer of Overdrive: their console task manager, a separate C23 project built with GCC 14 and CMake presets. It will replace its own terminal layer with Overdrive. It can only do that once another CMake project can build and link Overdrive. Today Overdrive has no install or export rules, its include directory is set for the build tree only, and its tests default on in any build that includes it. The owner asked for two routes: vendoring the source tree, and an installed package found by name.

## What Changes

- **New:** the Spec Kit feature directory `specs/003-packaging/`:
  - `spec.md`, with its quality checklist;
  - `plan.md`, `research.md` (R201–R211), `data-model.md`, and `quickstart.md`;
  - `contracts/consumer.md`, the CMake contract for consumers;
  - `tasks.md` (T201–T213) and `workgraph.json`, three issues, OVD-22 to OVD-24.
- The design proves both routes through CTest tests. Every CI job already runs them, so `.github/` does not change.

## Impact

Documentation only. No library code, build file, or public header changes in this transaction. The implementation issues are created after this merges, with the owner's approval, which was given in the session that wrote it.
