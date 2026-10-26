## Context

See `proposal.md` for motivation. The constitution's Art. XVI rules govern
every change, and the guard hook enforces the merge gate on
`gh pr merge`. The tx plugin cuts `tx-<slug>`, pushes
`<type>/[<scope>/]<slug>`, and squash-merges after CI is green.

## Goals / Non-Goals

**Goals:** make the tx lifecycle and the constitution agree without
weakening the issue-to-PR trace or the merge gate.

**Non-Goals:** changing the guard hook, the deny rules, or the merge
gate; changing repository visibility or plan.

## Decisions

- **Issue number in the transaction slug.** `/tx:open <number>-<slug>`
  keeps branch-to-issue mapping for claims and readiness. Alternative:
  keep `<number>-<slug>` branches and not use tx. Rejected by the owner.
- **Spec Kit keeps requirements; OpenSpec records transactions.** Two
  requirement stores would drift. Feature transactions set
  `skip_specs: true` unless a later amendment moves requirements into
  OpenSpec. Tension: OpenSpec's guidance asks for delta specs whenever
  behavior changes; the constitution wins, and this is labeled an
  assumption for owner review.
- **Reword, don't re-ignore.** The stock Spec Kit skills cite ignore
  patterns as code spans; they become plain escaped text. Changing
  `.gitignore` would stop ignoring real build output.
- **Adoption transaction slug.** This transaction predates the rule it
  enacts, so its slug carries no issue number; it traces to its issue
  through `Closes #<number>`.

## Risks / Trade-offs

- [Stock Spec Kit skills drift from upstream] → the reworded lines are
  the only edits; a re-sync re-applies them, and `docs-form-check` flags
  any regression.
- [Ruleset unsupported on a private free-plan repository] → the guard
  hook and agent discipline enforce the gate meanwhile (`swarm.md`).
