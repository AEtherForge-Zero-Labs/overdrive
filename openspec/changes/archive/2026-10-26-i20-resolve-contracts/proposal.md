## Why

The 001 spec, its research decisions, and the proposed public headers
contradicted one another or left behavior undefined: Unicode
segmentation, clipping, failure semantics, terminal restoration, and
allocation guarantees (issue #20). OVD-01 freezes `include/overdrive/`
from these headers, so the contracts must be consistent before it starts.

## What Changes

- `specs/001-render-core/research.md` gains §E, decisions R15–R36, and
  R5, R8, R10, R11 point to them.
- `spec.md`: FR-001, FR-011, FR-012, FR-013, FR-022, FR-024, FR-030,
  FR-031, SC-003, SC-004, edge cases, and assumptions restated to match.
- Proposed headers (`contracts/include/overdrive/`): the nullptr policy,
  `ovd_frame_end` as the single named exception to unchanged-on-failure,
  allocation sites, sink re-entrancy, the restoration sequence, closed
  canvases, cut wide glyphs, pairing repair, grapheme rules, and
  overflow-safe geometry.
  - **BREAKING (proposed contract):** `OvdAllocator.reallocate` is
    removed.
- `tasks.md`, `plan.md`, and `docs/architecture.md` follow.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

No code exists yet. OVD-01 copies the revised headers. The decisions are
labeled `assumption` for the owner's after-the-fact review.
