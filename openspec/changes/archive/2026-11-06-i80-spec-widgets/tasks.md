## 1. Specification

- [x] 1.1 Write `specs/004-widgets/spec.md` and `checklists/requirements.md`, and pass the checklist with no clarification markers.

## 2. Plan

- [x] 2.1 Write `plan.md`, `research.md`, `data-model.md`, `quickstart.md`, and `contracts/include/overdrive/widgets.h`. Compile the header under the project's strict flags, and tag every claim VERIFIED, INFERRED, or ASSUMED.

## 3. Tasks and analysis

- [x] 3.1 Write `tasks.md` (T301 onward) and `workgraph.json` (OVD-25 to OVD-30), with disjoint `owns` lists apart from the shared append-only files.
- [x] 3.2 Check that every FR and SC maps to at least one task with a test, and that every ASSUMED item in `research.md` has a task that checks it. Fix the gaps. The check found and fixed these:
  - SC-005's fuzz count (10⁷ per widget) could not fit the test timeout under the sanitizers, so it was lowered to 10⁵ with edge-weighted generation (R320);
  - the table needs the list's correction, so a private `src/list.h` was added;
  - stubs were dropped as unneeded;
  - a draw writes the prompt's NUL, so R312 was reworded;
  - cut text fills from the left, whatever the alignment;
  - the cross-feature file ownership with 003's OVD-23 and OVD-24 became dependencies.
