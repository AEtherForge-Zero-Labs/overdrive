## 1. Decisions

- [x] 1.1 Record R15–R36 in research.md §E; point R5, R8, R10, R11 to them
- [x] 1.2 Restate the affected requirements, success criteria, edge cases, and assumptions in spec.md

## 2. Contracts

- [x] 2.1 Revise core.h, terminal.h, render.h comments and remove `OvdAllocator.reallocate`
- [x] 2.2 Each header compiles alone and twice under gcc-14 and clang with `-std=c23 -Wall -Wextra -Wpedantic -Werror`

## 3. Alignment

- [x] 3.1 Align tasks.md, plan.md, and docs/architecture.md
- [x] 3.2 openspec validate --strict passes and the docs form check is clean
