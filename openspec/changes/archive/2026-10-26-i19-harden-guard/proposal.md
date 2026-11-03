## Why

The guard hook is the only mechanical enforcement of Art. XVI for agents,
and it fails open: a missing `jq`, an unparseable hook input, or a failed
`gh` lookup lets a command through. Its regexes miss common spellings of
the commands it exists to stop, such as a PR number after a flag, global
`gh` flags, `bash -c` wrappers, `refs/heads/main`, and `--method=DELETE`.
Nothing tests it. Nothing stops an agent from editing the guard or its
settings.

## What Changes

- The guard fails closed. A missing `jq`, invalid hook input, a missing
  or non-string command, a failed lookup, or a hook timeout or crash
  blocks the command (`onFailure: "block"` on the hook entry).
- Merges are allowed only in one canonical form,
  `gh pr merge <N> [--squash|--merge|--rebase] [--delete-branch|-d]`. The
  gate runs on exactly PR `<N>`. It requires the PR open against `main`
  from this repository, no reserved path, no `needs-human` label on the PR
  or its closing issues, and all checks green. A trusted
  `Review-Verdict: PASS` comment must name the current head in
  `Reviewed-Head:`, and no `FAIL` verdict may name that head.
- Pushes are allowed only as `git [-C <dir>] push [-u] origin <ref>` or a
  bare `git push`. Force, deletion, mirror/all/prune/tags, `main`, tag,
  `claude/*`, and non-`origin` destinations are blocked. The trailer check
  examines the commits actually being pushed.
- Mutating `gh api` calls are limited to an allowlist: comments, labels,
  reactions, and creating a ruleset. GraphQL mutations are blocked.
- The human-needed `gh` blocks survive quoting and wrapping. They now also
  cover `label delete|edit`, `workflow disable|run`, ruleset mutations,
  `gh auth` other than `status`, alias creation, and removal of the
  `needs-human` label.
- Secrets: token printing, bare `env`/`printenv`, token variable
  expansion, and the gh hosts file are blocked.
- Self-protection: Bash commands that mention the guard or the Claude
  settings must be read-only. Edit and Write deny rules cover the same
  files.
- An offline test suite, `.claude/hooks/test_guard.sh`, runs in a new
  `guard` workflow on Linux and macOS (`/bin/bash` 3.2). Its aggregate job
  is `guard-tests`.
- The process docs describe the canonical merge, the review-comment
  protocol, the `--body-file` rule, and what the server enforces versus
  the guard.

## Capabilities

### New Capabilities

None. Agent tooling and process documentation only; the library's
behavior does not change.

### Modified Capabilities

None.

## Impact

`.claude/hooks/guard.sh` and `.claude/settings.json` are owner-reserved,
so the owner merges. After the merge, only the owner can change the guard.
The new `guard-tests` job neither builds nor tests the library. The
server-side ruleset that pins required checks to GitHub Actions is
proposed in the PR body and applied by the owner after the merge.
