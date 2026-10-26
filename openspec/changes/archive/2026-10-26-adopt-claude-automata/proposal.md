## Why

The owner adopted claude-automata as the autonomy harness. Its tx plugin
lands every change as a transaction and blocks edits on `main` outside
one, but the constitution (Art. XVI) fixed issue branches as
`<issue-number>-<slug>`. That made every transaction a rule violation,
and the seed's CI and OpenSpec scaffold were not yet in the repository.

## What Changes

- Seed artifacts land: the `openspec/` scaffold and the
  `.github/workflows/memory-check.yml` workflow (`openspec-validate`,
  `docs-form-check`).
- Constitution v1.4.0, ratified by the owner: one issue, one
  transaction, one pull request; transaction branches carry the issue
  number; OpenSpec records transactions while Spec Kit keeps
  requirements; the plan comment links the OpenSpec change.
- `AGENTS.md`, `CLAUDE.md`, and `docs/process/swarm.md` describe the
  agent loop in terms of `tx:open` … `tx:close`.
- Two stock Spec Kit skills stop citing gitignored paths as code spans,
  so `docs-form-check` passes.

## Capabilities

### New Capabilities

None. This is process and tooling; no product behavior changes.

### Modified Capabilities

None.

## Impact

Owner-reserved path (the constitution), so the owner merges this pull
request. Server-side branch protection stays `unsupported` while the
repository is private on the free plan; the next `/tx:open` after a
visibility or plan change lands the ruleset.
