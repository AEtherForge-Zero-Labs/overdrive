## Context

`.claude/hooks/guard.sh` runs as a Claude Code `PreToolUse` hook on every
Bash call, including calls from subagents. Exit 2 blocks the call. Any
other failure, including a timeout, lets the call through unless the hook
entry sets `onFailure: "block"` (Claude Code hooks reference). The hook
receives JSON on stdin with `tool_input.command` and runs in Claude's
current directory. The guard must run under macOS `/bin/bash` 3.2 and
bash 5, and its tests must run offline.

## Goals / Non-Goals

**Goals:**

- Every lookup the guard relies on blocks when it fails.
- One accepted spelling for each high-risk action: merge, push, and
  mutating API call. Detection is broad; acceptance is narrow.
- Deterministic offline tests that cover every rule in both directions.

**Non-Goals:**

- A sandbox. A command built at run time (variables, command
  substitution, a script on disk) can evade any text matcher. The guard
  is a tripwire for mistakes and casual spellings. The server-side
  ruleset is the boundary for what it covers.
- Separate identities for the owner and the agents. That needs the owner.

## Decisions

- **Detection over normalized words, acceptance by exact shape.** Quotes
  and backslashes are deleted, and the text is split into words at
  whitespace and shell punctuation, so `bash -c "…"`, `eval '…'`,
  `/path/to/gh`, env prefixes, and global flags all yield the same words.
  Program names (`gh`, `git`) match case-insensitively, because a
  case-insensitive file system resolves `GH` to `gh`. Subcommands match
  case-sensitively; gh 2.102 and git 2.53 reject `PR` and `STATUS`
  (measured). A detected merge or push must then match an exact regex
  over the whole command. Rejected alternative: extend the blocklist
  regexes, which is the approach that failed.
- **A jq tokenizer where structure matters.** The read-only check for
  protected paths and the `gh api` parser need quote-aware words and
  command boundaries. jq is already a dependency, and its regex engine
  is the same on both platforms. A bash-only tokenizer would be slower
  and harder to keep correct under bash 3.2.
- **The review is bound to the head by a comment protocol.** All agents
  share one GitHub account, so GitHub approvals are impossible. A PASS
  counts only from an `OWNER`, `MEMBER`, or `COLLABORATOR` comment,
  because the repository is public and anyone may comment. A FAIL from
  any author blocks that head.
- **Changed files come from the REST files endpoint.** It is paginated
  and reports `previous_filename`, so renaming a reserved file away is
  caught. The guard compares the count with `changedFiles`.
- **Push ranges use the remote's actual state.** `git ls-remote` gives the
  remote branch's sha. A new branch is checked against `origin/main`, and
  the full history when `main` does not exist yet. The source must be
  `HEAD` or a local branch that is not also a tag, because git puts a
  tag source's destination under `refs/tags/`. `push.followTags` blocks.
  A bare `git push` resolves through `@{push}` and refuses configured push
  refspecs, mirror, and `push.default=matching`.
- **Self-protection on two layers.** The guard blocks a non-read-only Bash
  command that mentions the protected files. `Edit(//**/…)` deny rules
  cover the Edit and Write tools anywhere on the file system. The `/`
  anchor would resolve against the session's primary working directory,
  but the hook that runs is `$CLAUDE_PROJECT_DIR`'s, and a worktree agent
  works in a different directory. `disableAllHooks: false` in project
  settings overrides a `true` in user settings.
- **Removing `needs-human` is the owner's.** The gate reads that label,
  so the guard blocks `--remove-label needs-human`, the API delete, and
  label edits and deletes.

## Risks / Trade-offs

- [Text that mentions a guarded command is blocked, e.g. a PR title with
  "pr merge"] → Put the text in a file: `--body-file`, `-F`, or the Write
  tool. The block message says so.
- [Read-only access to the guard files is narrower than before: no
  `grep -i`, no redirection except to `/dev/null`] → These are the spec's
  limits; reading through the Read tool is unaffected.
- [Merge gate latency: about four GitHub calls plus one per closing
  issue] → Measured in the PR. The 20 s timeout now blocks rather than
  allows.
- [One account for owner and agents] → No server-side identity
  separation. Documented in `docs/process/swarm.md` as an owner decision.
