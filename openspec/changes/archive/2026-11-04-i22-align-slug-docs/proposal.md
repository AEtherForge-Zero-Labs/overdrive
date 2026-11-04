## Why

Issue [#22](https://github.com/AEtherForge-Zero-Labs/overdrive/issues/22)
identified that transaction slugs starting with an issue number were
rejected by the tx plugin. The owner merged the constitution-only
amendment in [#23](https://github.com/AEtherForge-Zero-Labs/overdrive/pull/23)
on 2026-10-26. The follow-up documentation and ignore-rule changes are in
[#25](https://github.com/AEtherForge-Zero-Labs/overdrive/pull/25), commit
`0968bf9a0ada6b02c31cb38f6756d59f19805022`.

**Retrospective reconciliation, written 2026-11-04:** the original
follow-up was implemented without an issue plan comment or an OpenSpec
transaction record. This record is being added after implementation,
following the owner's instruction to document the gap. It does not
backdate a plan, establish that planning preceded implementation, waive
Article XVI, or declare the earlier process compliant.

## What Changes

### Existing implementation, observed rather than newly performed

- `AGENTS.md`, `CLAUDE.md`, and `docs/process/swarm.md` in #25 use
  letter-prefixed issue slugs: `i<number>-<slug>`.
- The ignore rule in #25 excludes nested Claude Code agent worktrees.
- #23 already changed the constitution. This follow-up does not amend it.

### Current recovery work

- Add this proposal, the design reconciliation, task status, and actual
  creation metadata under `openspec/changes/i22-align-slug-docs/`.
- Post a current comment on #22 describing the missing records and
  distinguishing completed work from remaining gates.
- Keep existing implementation and review history intact. Do not
  manufacture an earlier claim, transaction opening, plan, or proof run.

The recovery branch `tx-i22-align-slug-docs` was cut on 2026-11-04 from
current `origin/main`, commit
`244fdebdc62f732dba22850b9aff61d53afe0fdd`. It is preparation for the
existing #25, not a second pull request or evidence of an original
transaction. The record is initially local; its creation does not update
the published PR.

## Capabilities

### New Capabilities

None. These are process records, not library requirements.

### Modified Capabilities

None. No OpenSpec requirement deltas are needed (`skip_specs: true`).

## Impact

No library source, public API, build configuration, check, guard, or
owner-reserved file changes are part of this recovery. Existing archives
remain frozen.

### Plan for the recovery, not a pre-implementation plan for #25

Files:
- Existing #25: `AGENTS.md`, `CLAUDE.md`, `docs/process/swarm.md` align
  transaction examples; `.gitignore` excludes agent worktrees.
- New record: `openspec/changes/i22-align-slug-docs/.openspec.yaml`,
  `proposal.md`, `design.md`, and `tasks.md` record provenance, scope,
  decisions, evidence, and remaining work.
- GitHub: a new #22 comment records this reconciliation.

Boundaries: Not applicable — no library or runtime contract changes.

Public API: Not applicable — no symbols or public types change.

Paradigm: Not applicable — no module is added.

New pieces: One OpenSpec record, required to preserve the missing
transaction history; no option or dependency is added.

Tensions: Article XVI requires a plan before implementation; that order
cannot be restored retroactively. Preserve the deviation explicitly
rather than claim that writing this record cures the historical failure.

Assumptions: Keep #25 as the follow-up PR and prepare this record for
inclusion there; do not create another PR for #22. This is the narrowest
recovery that preserves the existing implementation and review history.

Human needed: The constitution amendment was merged by the owner in #23.
The issue no longer carried `needs-human` when inspected during this
recovery; this agent did not remove it. No new owner-reserved change is
proposed, and the current instruction does not authorize landing #25.

Proof:
- `npx --yes @fission-ai/openspec@1.7.0 validate --all --strict --no-interactive`
- `git diff --cached --check`
- The unchanged docs-form-check Python body from the repository workflow,
  executed locally against the staged record.
- Read back the posted #22 comment and compare it with this reconciliation.

## Remaining before #25 can land

- Incorporate this record and current `main` into the existing published
  branch without rewriting its history.
- Run the applicable checks on the resulting head and record actual
  commands and output.
- Obtain a fresh independent review that resolves the existing failed
  review's findings.
- Archive the completed record before transaction close and recheck the
  resulting head. Do not mark incomplete work done merely to archive it.
- Confirm all merge gates and obtain explicit landing authorization.

This recovery alone does not make #22 complete or #25 merge-ready.
