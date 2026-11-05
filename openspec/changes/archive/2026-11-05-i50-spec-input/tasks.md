## 1. Specification

- [x] 1.1 `specs/002-input-restore/spec.md` written from the feature description, with the owner's three clarifications recorded in §Clarifications and no `[NEEDS CLARIFICATION]` marker left
- [x] 1.2 `checklists/requirements.md` passes every item

## 2. Plan

- [x] 2.1 `research.md`: decisions R101–R117, each external claim tagged VERIFIED, INFERRED, or ASSUMED, and the tensions table
- [x] 2.2 `plan.md`: constitution check, public API, modules, terminal-mode table, workstreams
- [x] 2.3 `data-model.md` and `quickstart.md`
- [x] 2.4 `contracts/include/overdrive/input.h` and `terminal.h`; each compiles alone and twice under gcc-14 and clang with `-std=c23 -Wall -Wextra -Wpedantic -Werror -Wconversion` (design D5)

## 3. Tasks and analysis

- [x] 3.1 `tasks.md` from `speckit-tasks`, cut so parallel issues touch disjoint files
- [x] 3.2 `speckit-analyze` run; every CRITICAL and HIGH finding fixed in the artifacts

## 4. Checks

- [x] 4.1 `openspec validate --all --strict` passes and the docs form check is clean
- [x] 4.2 The diff touches only `specs/002-input-restore/`, this change, and the three docs that point at feature 002 (`docs/README.md`, `README.md`, `docs/architecture.md`)
