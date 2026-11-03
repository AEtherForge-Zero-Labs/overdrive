## 1. Decisions

- [x] 1.1 R26: fall back to the size read at create when no frame has ended
- [x] 1.2 R28: state that the enter-failure exit sequence includes `CSI ? 1049 l` when the alternate screen was configured
- [x] 1.3 R34: drop the ECMA-48 attribution; tag the mode-2026 claim ASSUMED
- [x] 1.4 spec.md: the sink edge case says only begin, end, and set_size return `OVD_ERROR_STATE` (R24)
- [x] 1.5 Rewrap the long prose lines in research.md §E (R19, R22, R30) and spec.md (SC-003)

## 2. Contracts

- [x] 2.1 Update the `ovd_terminal_create` and `ovd_terminal_destroy` doc comments in both `terminal.h` copies; the copies stay byte-identical
- [x] 2.2 Each of the four headers in both roots compiles alone and twice under gcc-14 and clang with `-std=c23 -Wall -Wextra -Wpedantic -Werror -Wconversion`

## 3. Checks

- [x] 3.1 `openspec validate --all --strict` passes and the docs form check is clean
- [x] 3.2 The debug preset builds and its tests pass
