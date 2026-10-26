## 1. Tests first

- [x] 1.1 Write `.claude/hooks/test_guard.sh` with fake `gh` and `git`, covering every defect in the issue and every block and allow rule
- [x] 1.2 Run the suite against the current guard and record the failing cases

## 2. Guard

- [x] 2.1 Fail closed on input, `jq`, and every lookup
- [x] 2.2 Canonical merge form and the head-bound merge gate
- [x] 2.3 Canonical push forms, destination rules, and the pushed-range trailer check
- [x] 2.4 `gh api` allowlist and GraphQL mutation block
- [x] 2.5 Human-needed `gh` blocks, secrets, and self-protection
- [x] 2.6 Suite passes under bash 5 and `/bin/bash` 3.2; `bash -n` is clean

## 3. Settings and CI

- [x] 3.1 `onFailure: "block"`, `disableAllHooks: false`, and Edit/Read deny rules in `.claude/settings.json`
- [x] 3.2 `.github/workflows/guard.yml` with the aggregate `guard-tests` job

## 4. Process docs

- [x] 4.1 `docs/process/swarm.md`: canonical merge, review protocol, server versus guard enforcement, the identity gap
- [x] 4.2 `AGENTS.md` Pull requests: canonical merge, review protocol, `--body-file` rule
