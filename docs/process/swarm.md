# Swarm Protocol

How many AI agents work on Overdrive at once without stepping on each
other or on the constitution. Subordinate to the constitution and
`AGENTS.md`; this file adds the coordination mechanics only.

## Roles

| Role | Who | Does |
|---|---|---|
| **Owner** | human (CODEOWNERS) | Ratifies, approves contracts, merges (unless delegated, below), runs manual terminal checks |
| **Dispatcher** | one agent session | Reads `workgraph.json`, creates issues (with owner approval), assigns ready issues to workers, watches CI, never writes product code |
| **Worker** | one agent per issue | Plans, implements, tests, opens a PR for exactly one issue |
| **Reviewer** | a separate agent session | Reviews a PR against the spec, contracts, constitution, and `checklists/identity-review.md`; never reviews its own work |

## Readiness

An issue is **ready** when every `depends_on` issue is merged into `main`,
it has no unanswered `open_decision`, and no other worker holds it.
Dispatcher computes readiness from `workgraph.json` plus GitHub state:

```bash
gh issue list --label task --state open --json number,title,labels,assignees
gh pr list --state merged --json number,title,closingIssuesReferences
```

## Claiming

1. Dispatcher labels the issue `agent:claimed` and comments
   `Claimed by <session-id> on <branch>`. One claim per issue.
2. Worker creates a worktree and branch `<number>-<slug>` from current
   `main`. Never push a `claude/*` branch.
3. Worker posts the plan comment (template in `AGENTS.md`). Under
   Art. XVI the issue's assignee agrees to it; with delegated authority
   (below), the reviewer agent's approval of the plan stands in.
4. A worker that halts posts the BLOCKED template and removes
   `agent:claimed`, adding `agent:blocked`.

## File discipline

- Touch only the paths in the issue's `owns` list, plus the shared
  append-only files (`CMakeLists.txt`, `tests/CMakeLists.txt`), where you
  only add lines.
- Need a file you don't own? Stop and say so on the issue (Art. IX). Never
  edit it "just a little".
- `include/overdrive/` is frozen after OVD-01. A worker that finds the
  contract wrong halts (Art. XII) and the dispatcher opens a
  contract-change issue for the owner.

## Pull requests

- One PR per issue, PR template filled in completely, `Closes #<number>`.
- Required checks: `ci-ok` (all platforms) and the exported-symbol check.
- The reviewer agent leaves a review comment with a verdict and the
  identity checklist results. A verdict is advice unless authority is
  delegated.
- After a merge, every open PR rebases on `main` before its next review.

## Merge authority

**As ratified-pending (constitution v1.2.0, Art. XVI): humans merge.** The
swarm runs to "PR green and reviewed", and the owner merges. This is the
default and the guard hook enforces it.

**Delegated mode (needs an amendment).** For fully autonomous waves, the
owner may amend Art. XVI to allow an agent to merge a PR when **all** of
these hold: required checks green on the PR head; a reviewer agent other
than the author approved; the PR changes no path under
`include/overdrive/`, `.specify/memory/`, `.github/`, `.claude/`, `AGENTS.md`,
or `CLAUDE.md`; and the issue has no `human_gate`. The amendment must also
change `.claude/settings.json` and `.claude/hooks/guard.sh`, as one
declared change (Art. XI). Until that amendment is ratified, delegated
mode does not exist.

## What agents cannot verify

Behavior in a real terminal (Windows Terminal, macOS Terminal, Linux
emulators) is never observed by an agent. Workers list it under
UNVERIFIED; the owner runs `docs/validation/manual-checklist.md`.

## Budgets

Attempt budgets are in `CLAUDE.md`. A worker that exhausts one halts with
BLOCKED; the dispatcher does not reassign the same issue to a fresh worker
without a changed plan or a decision from the owner.
