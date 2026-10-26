## Why

Constitution Art. VIII rule 2 barred AI and dev-assist tooling from every
CI path. That forbade the tx plugin's required `openspec-validate` job,
which validates agent records with the pinned OpenSpec CLI but neither
builds nor tests the library.

## What Changes

- Constitution v1.4.1 (ratified by the owner): rule 2 covers the
  library's build and test path and any CI job that builds or tests it. A
  pinned, deterministic, model-free validator of agent records may run in
  CI.
- `baseline.md` restates the rule with the same scope.
- Version citations in `docs/README.md` and `docs/process/swarm.md`
  follow.

## Capabilities

### New Capabilities

None. Governance wording only.

### Modified Capabilities

None.

## Impact

Owner-reserved path (the constitution), so the owner merges. No build,
test, or CI job changes.
