## 1. Historical work observed during recovery

These checked items describe existing artifacts, not work performed
under a pre-existing plan.

- [x] 1.1 Verify #23 was merged and contains the owner-ratified slug amendment.
- [x] 1.2 Inspect #25 commit 0968bf9a0ada6b02c31cb38f6756d59f19805022: three workflow documents use letter-prefixed issue slugs and the ignore rule excludes agent worktrees.
- [x] 1.3 Confirm the original follow-up has no issue plan comment or OpenSpec record; preserve that process deviation explicitly.

## 2. Current record-only recovery

- [x] 2.1 Create a retrospective proposal, design, task list, and metadata using the actual creation date.
- [x] 2.2 Post a current #22 comment identifying the local record location and separating observed work from remaining gates.
- [x] 2.3 Validate the OpenSpec records, run the unchanged docs-form check, check whitespace, and record actual results in design.md. OpenSpec and whitespace pass; docs-form fails on two unchanged GDB examples matched by the machine's global ignore rule.

## 3. Remaining transaction work, taken over on 2026-11-04

Sections 1 and 2 were written by the recovery session (Delta), in its
own checkout. On 2026-11-04 the owner told Claude Code session
overdrive-20 (claude-opus-5-5) to take #25 over and land it. That
session copied this record into #25's branch unchanged, except for this
section and design.md's "Takeover" section.

- [x] 3.1 Incorporate the record and current main into #25 without rewriting published history; review the complete resulting diff.
- [x] 3.2 Run applicable checks on the resulting head and record actual evidence. Results are in design.md under "Takeover".
- [x] 3.3 Archive the completed record before close.

Not tasks: these are merge gates, which `tx:close` checks on the final
head. Required CI must be green on that head, including `ci-ok` and
`guard-tests`. An independent review must resolve the failed verdict's
findings, with a PASS naming that head. Nothing above is a merge
verdict.
