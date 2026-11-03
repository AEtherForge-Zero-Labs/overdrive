## Why

The independent re-review of the 001 contract resolutions (issue #21)
passed with five non-blocking nits (issue #26). Two leave terminal
restoration undefined on paths that OVD-10 and OVD-11 implement: destroy
before any frame has ended, and the exit sequence after a failed enter
write. One attributes an unverified terminal behavior to ECMA-48 (Art. X).
They are settled before those issues start.

## What Changes

- `specs/001-render-core/research.md` §E:
  - R26: with no ended frame, the exit cursor move uses the size read at
    create.
  - R28: the best-effort exit sequence after a failed enter write counts
    the alternate screen as entered whenever it was configured, so it
    includes `CSI ? 1049 l`.
  - R34: the claim that unsupporting terminals ignore mode 2026 loses its
    ECMA-48 attribution and is tagged ASSUMED.
  - R19, R22, and R30 paragraphs are rewrapped.
- `specs/001-render-core/spec.md`: the sink re-entrancy edge case states
  that only begin, end, and set_size return `OVD_ERROR_STATE` (R24);
  SC-003 is rewrapped.
- `terminal.h`, both the proposed copy in `specs/001-render-core/contracts/`
  and the public copy in `include/overdrive/`: the `ovd_terminal_create`
  and `ovd_terminal_destroy` doc comments follow R26 and R28. Comments
  only; no symbol, type, or signature changes.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

No code implements R26, R28, or R34 yet; OVD-10 (#11) and OVD-11 (#12)
will. The public header changes in doc comments only, so the ABI and API
are unchanged. The decisions shape observable behavior, so the PR is
labeled `assumption` for the owner's after-the-fact review.
