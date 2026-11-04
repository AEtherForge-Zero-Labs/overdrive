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
2. Worker creates a worktree and opens the issue's transaction,
   `/tx:open i<number>-<slug>`, from current `main`. Never push a
   `claude/*` branch.
3. Worker records the change with `/tx:plan`, posts the plan comment
   (template in `AGENTS.md`) linking it, and proceeds; assumptions are
   documented, not waited on (Art. IX).
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
- Required checks: `ci-ok` (all platforms), the exported-symbol check,
  `guard-tests`, and the tx plugin's `openspec-validate` and
  `docs-form-check`.
- The reviewer agent leaves a review comment with the identity checklist
  results and, each on its own line, `Review-Verdict: PASS` (or `FAIL`)
  and `Reviewed-Head: <sha>`, the full 40-character head commit it
  reviewed. A new push needs a new review.
- Text that mentions a guarded command (a merge, a push, `gh api`, a
  token variable) goes in a file written with the Write tool and passed
  with `--body-file` or `-F`. The guard reads the whole command line,
  quoted text included.
- After a merge, every open PR rebases on `main` before its next review.

## Merge authority

Constitution v1.4.2, Art. XVI: **agents merge** when all of these hold —

1. every required check is green on the PR's head commit;
2. an agent session other than the author reviewed the PR and found no
   unresolved defect (all agents share one GitHub account, so the review
   is a PR comment with a verdict, not a GitHub approval);
3. neither the PR nor its issue carries `needs-human`;
4. the PR changes no owner-reserved path: the constitution, `LICENSE`,
   `.github/CODEOWNERS`, `.claude/hooks/guard.sh`, `.claude/settings.json`.

The only merge command is the canonical form, run alone:

```bash
gh pr merge <N> [--squash|--merge|--rebase] [--delete-branch|-d] --match-head-commit <sha>
```

`<sha>` is the full head commit the review names. `.claude/hooks/guard.sh`
blocks every other spelling of a merge (flags before the number, `-R`,
an env prefix, a wrapper such as `bash -c` or `xargs`, `--auto`,
`--admin`, the merge API, a GraphQL mutation). For the canonical form,
it checks PR `<N>`: it is open against `main` from this repository, its
head is `<sha>`, its checks are all green, it changes no reserved path
(renames included), neither it nor its closing issues carry
`needs-human`, and a member's comment says `Review-Verdict: PASS` with
`Reviewed-Head:` equal to that head, with no `FAIL` for it. GitHub then
refuses the merge if the branch moved after the check. `/tx:close`
performs the squash merge in this form, so the review comes before it.
The squash title is the PR title.

Pushes are allowed only as `git [-C <dir>] push [-u] origin <src>[:<dst>]`
or a bare `git push`. Only the owner removes `needs-human`.

The guard parses every gh and git command in a command line. Text in
arguments is only text: a title or a grep pattern that says "merge" is
not a merge. It blocks what it cannot read: an unknown gh or git
subcommand, a flag before the subcommand, a git alias, a `$`-expansion
or substitution where the subcommand goes, and, in any command that
names gh, git, an environment dump, `.claude`, or the gh configuration,
line continuations, `$'…'`, `eval`, `sh -c`, and inline interpreter
code.

A human is needed only for the Art. XVI list: constitution changes,
license/visibility/ownership, publishing, destructive operations,
secrets and org settings, weakening protection, and real-terminal
observations. Agents label those `needs-human` and keep working on other
ready issues.

**What the server enforces, and what only the guard enforces.** The
repository is public, so GitHub enforces its rulesets on `main`: changes
arrive only through pull requests, with no force-push or deletion, and
the required checks must be green on an up-to-date head. A ruleset that
pins the required checks to the GitHub Actions app makes a status posted
through the API unable to satisfy them; rulesets are the owner's to
create and change. Everything else
in the gate holds only because the guard hook runs: the review verdict,
`needs-human`, reserved paths, commit trailers, and the human-needed
commands.

**The guard is a tripwire, not a security boundary.** It reads command
text. A command assembled at run time (from a variable, a script file,
or a pipe into a program) can evade it, and so can any process that
does not go through Claude Code's Bash tool. It catches mistakes and
plain attempts; it does not stop a determined agent. The boundary is the
server-side ruleset plus a separate agent identity: agents on their own
account or GitHub App, without admin rights, so the server itself
refuses what an agent may not do.

The remaining gap is identity. The owner and every agent use one GitHub
account, so the server cannot tell an agent's merge from the owner's, and
code-owner review cannot be required (nobody can approve their own pull
request). Closing it needs the owner: agents on a bot account or a
GitHub App without admin rights, and CODEOWNERS review required on the
reserved paths.

**Who changes the guard.** Art. XVI makes weakening the guard
human-needed, and `.claude/hooks/guard.sh` and `.claude/settings.json`
are owner-reserved paths, so a pull request that changes them needs the
owner's merge. Edit deny rules cover `.claude/hooks/`, the Claude
settings files, and the gh configuration, and the guard blocks any Bash
command that touches the hooks, the settings, or the `.claude` directory
as a whole unless it is read-only. Like the rest of the guard, these
are tripwires: they keep agents from changing the guard by accident or
in plain sight, not from every way a process can write a file.

## What agents cannot verify

Behavior in a real terminal (Windows Terminal, macOS Terminal, Linux
emulators) is never observed by an agent. Workers list it under
UNVERIFIED; the owner runs `docs/validation/manual-checklist.md`.

## Budgets

Attempt budgets are in `CLAUDE.md`. A worker that exhausts one halts with
BLOCKED; the dispatcher does not reassign the same issue to a fresh worker
without a changed plan or a decision from the owner.
