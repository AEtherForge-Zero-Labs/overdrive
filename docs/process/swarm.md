# Swarm Protocol

How many AI agents work on Overdrive at once without stepping on each
other or on the constitution. Subordinate to the constitution and
`AGENTS.md`; this file adds the coordination mechanics only.

## Roles

| Role | Who | Does |
|---|---|---|
| **Owner** | human (CODEOWNERS) | Only Art. XVI's human-needed items: ratification, license, publishing, destructive ops, secrets, real-terminal checks; reviews `assumption`-labeled PRs after the fact |
| **Dispatcher** | one agent session | Reads `workgraph.json`, keeps issues in sync with it, dispatches ready issues to workers, watches CI, never writes product code |
| **Worker** | one agent per issue | Plans, implements, tests, opens a PR for exactly one issue |
| **Reviewer** | a separate agent session | Reviews a PR against the spec, contracts, constitution, and `checklists/identity-review.md`; never reviews its own work |

## Readiness

An issue is **ready** when every `depends_on` issue is merged into `main`,
it has no `needs-human` label, and no other worker holds it.
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
3. Worker posts the plan comment (template in `AGENTS.md`) and proceeds;
   assumptions are documented, not waited on (Art. IX).
4. A worker that halts posts the BLOCKED template and removes
   `agent:claimed`, adding `agent:blocked`.

## File discipline

- Touch only the paths in the issue's `owns` list, plus the shared
  append-only files (`CMakeLists.txt`, `tests/CMakeLists.txt`), where you
  only add lines.
- Need a file you don't own? Stop and say so on the issue (Art. IX). Never
  edit it "just a little".
- `include/overdrive/` is frozen after OVD-01. A worker that finds the
  contract wrong stops that issue and the dispatcher opens a
  contract-change issue; it is worked, reviewed, and merged like any
  other, with the change recorded in `research.md` and labeled
  `assumption`.

## Pull requests

- One PR per issue, PR template filled in completely, `Closes #<number>`.
- Required checks: `ci-ok` (all platforms) and the exported-symbol check.
- The reviewer agent leaves a review comment with a verdict and the
  identity checklist results.
- After a merge, every open PR rebases on `main` before its next review.

## Merge authority

Constitution v1.3.0, Art. XVI: **agents merge** when all of these hold —

1. every required check is green on the PR's head commit;
2. an agent session other than the author reviewed the PR and found no
   unresolved defect (all agents share one GitHub account, so the review
   is a PR comment with a verdict, not a GitHub approval);
3. neither the PR nor its issue carries `needs-human`;
4. the PR changes no owner-reserved path: the constitution, `LICENSE`,
   `.github/CODEOWNERS`, `.claude/hooks/guard.sh`, `.claude/settings.json`.

`.claude/hooks/guard.sh` checks 1, 3, and 4 mechanically on every
`gh pr merge`, and refuses `--admin`. Merge with
`gh pr merge <number> --squash --delete-branch`.

A human is needed only for the Art. XVI list: constitution changes,
license/visibility/ownership, publishing, destructive operations,
secrets and org settings, weakening protection, and real-terminal
observations. Agents label those `needs-human` and keep working on other
ready issues.

**Server-side protection.** On a private repository under the free plan,
GitHub does not enforce rulesets, so the gate above is enforced only by
the guard hook and agent discipline. Making it server-enforced needs the
repository public or a paid plan, which is an owner decision.

## What agents cannot verify

Behavior in a real terminal (Windows Terminal, macOS Terminal, Linux
emulators) is never observed by an agent. Workers list it under
UNVERIFIED; the owner runs `docs/validation/manual-checklist.md`.

## Budgets

Attempt budgets are in `CLAUDE.md`. A worker that exhausts one halts with
BLOCKED; the dispatcher does not reassign the same issue to a fresh worker
without a changed plan or a decision from the owner.
