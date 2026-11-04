## Context

The chronology and recovery plan are in `proposal.md`. This is a
retrospective record for issue #22 and existing PR #25, written on
2026-11-04, after the implementation it describes.

Verified sources read during recovery:

- #22's issue body, labels, assignment state, and comments.
- #23's merged state and constitution amendment.
- #25's complete four-file diff, commit message, and review comments.
- The repository's constitution, agent instructions, and existing
  OpenSpec archive conventions.

The reason the original plan and record were omitted is unknown. Do not
invent a cause or infer an exemption from the change being documentation.

## Goals / Non-Goals

**Goals:** make the omission and current recovery auditable; preserve
the original implementation; distinguish observed historical work from
new work and checks still outstanding.

**Non-Goals:** change the constitution, weaken checks, alter guard
policy, rewrite published commits, update the published branch, or merge
the PR as part of this record-only instruction.

## Decisions

- **Record the deviation, do not erase it.** Use the actual creation date.
  An earlier commit and a later plan remain in that order. Editing old
  comments to look contemporaneous or claiming an earlier transaction
  opening would misrepresent the evidence.
- **Retain the existing follow-up PR.** Prepare one record for #25 rather
  than open a replacement PR or pretend that #23 and #25 were historically
  one transaction. #23 was the owner-only constitution amendment; #25 is
  its documentation and ignore-rule follow-up.
- **Keep the recovery narrow.** Only this active OpenSpec directory and
  a current issue comment are added now. The newly opened local recovery
  branch does not itself update #25.
- **No requirement deltas.** Set `skip_specs: true`, following existing
  process-only records. There is no new library behavior to specify.
- **Do not inherit verification claims.** Earlier PR comments and CI are
  historical evidence, not locally executed checks for this record.
  Add actual recovery verification results below when available.

## Risks / Trade-offs

- A retrospective record cannot satisfy the historical plan-before-code
  requirement. Keep that limitation visible for the next reviewer.
- The local recovery branch and the published #25 branch differ.
  Integration, checks, and independent review remain separate work.
- The prior review's failed verdict is not superseded by this authoring
  session. A separate reviewing session must assess the resulting PR head.

## Recovery verification

The issue comment was posted at
https://github.com/AEtherForge-Zero-Labs/overdrive/issues/22#issuecomment-6633677398.
Reading it back through the GitHub API and comparing its body with
`proposal.md` confirmed a match before this verification section was
written. The comment states that the record is initially local.

Commands run in this recovery:

```text
npx --yes @fission-ai/openspec@1.7.0 validate --all --strict --no-interactive
✓ change/i22-align-slug-docs
Totals: 1 passed, 0 failed (1 items)

git diff --cached --check
(no output; exit 0)
```

The unchanged Python body of the repository workflow was executed with:

```python
from pathlib import Path
from textwrap import dedent
workflow = Path('.github/workflows/memory-check.yml').read_text()
body = workflow.split("          python3 - <<'PY'\n", 1)[1].split('\n          PY', 1)[0]
exec(compile(dedent(body), '.github/workflows/memory-check.yml:docs-form-check', 'exec'))
```

**Docs form check: FAIL (Python exit 1).** The two diagnostics are in the
unchanged GDB scripting reference:

```text
.agents/skills/gdb/references/scripting.md: references gitignored path ~/.gdb_scripts/
.agents/skills/gdb/references/scripting.md: references gitignored path ~/.gdbinit
```

Investigation with `git check-ignore -v` showed both are matched by this
machine's global ignore file, line 1, pattern `*~`, rather than a rule
added by this recovery. The GDB reference, workflow, and repository
ignore file have no diff against the recovery branch's base. No ignore
configuration, check, or unrelated document was changed to hide the
failure. This local result is not a prediction of CI.

No library build, runtime test, independent review of the new record, or
PR integration was performed. All landing gates remain separate work.

## Takeover (2026-11-04, Claude Code session overdrive-20)

The owner instructed this session to take #25 over and land it. The
recovery session's record was copied into #25's branch as written,
except for tasks.md section 3 and this section. The published commit
0968bf9 is not rewritten.

How the prior review's FAIL findings stand:

1. **`needs-human` on #22.** The label is gone: `gh issue view 22`
   shows only `task`. This session did not remove it.
2. **Behind main, missing checks.** main (9c5d21b) is merged into the
   branch as a merge commit, with no conflicts. The net diff against
   main is still the original four files: `.gitignore`, `AGENTS.md`,
   `CLAUDE.md`, and `docs/process/swarm.md` (7+/4−). The pushed head
   gets the full CI set, `ci-ok` and `guard-tests` included.
3. **No plan or transaction record.** This record is that record,
   written after the fact and labeled as such. It does not cure the
   original order (see Risks).

Evidence on the merged tree, macOS arm64:

```text
tx-hook openspec validate --all --strict --no-interactive --json
  → valid: true, failed: 0
git diff --cached --check
  → (no output; exit 0)
bash .claude/hooks/test_guard.sh
  → 498 passed, 0 failed
git grep for old-form slugs (tx-<number>, tx:open <number>, <number>-<slug>)
  → only .claude/hooks/test_guard.sh fixtures such as tx-5-x, which are
    arbitrary branch names for push parsing, not the documented rule
git check-ignore -v .claude/worktrees/x
  → .gitignore:41:.claude/worktrees/   .claude/worktrees/x
```

The docs-form check, run with the workflow's unchanged body, fails
locally on the same two GDB lines as above. `git check-ignore -v
--no-index` traces the match to pattern `*~` on line 1 of this
machine's global ignore file (`core.excludesFile`), not to a
repository rule. The same unchanged files pass CI's
docs-form-check (green on PR #42). Nothing was changed to hide it.
