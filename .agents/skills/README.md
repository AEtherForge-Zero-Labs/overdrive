# Agent skills

Shared skills for every AI tool used on Overdrive. Each skill is
`<name>/SKILL.md`. Claude Code finds them through pointer files in
`.claude/skills/` (pointers, not symlinks, because symlinks break on
Windows checkouts). Other tools should read the file when a human asks for
the skill by name.

The `speckit-*` skills are the stock GitHub Spec Kit commands installed by
Spec Kit into `.claude/skills/`. They are not duplicated here. Never run
`specify init` again in this repository: it could overwrite the
constitution.

## Reference skills (vendored, read-only)

Copied unchanged, apart from a copied `LICENSE`. To update one, re-copy it
from its source at a newer commit and review the diff. Don't edit it in
place.

| Skill | Use it for | Source (license) |
|---|---|---|
| `cmake`, `ninja`, `gcc` | Build files, presets, compiler flags and errors | [mohitmishra786/low-level-dev-skills](https://github.com/mohitmishra786/low-level-dev-skills) @ `bdc5847` (MIT) |
| `gdb`, `valgrind`, `sanitizers` | Crashes, leaks, out-of-bounds access, undefined behavior | same |
| `static-analysis` | clang-tidy, cppcheck, scan-build | same |
| `systematic-debugging` | Any bug or build failure, *before* proposing a fix | [obra/superpowers](https://github.com/obra/superpowers) @ `bb92a77` (MIT) |
| `verification-before-completion` | Before claiming anything works (backs Art. X) | same |
| `requesting-code-review`, `receiving-code-review` | Reviewing a PR; acting on review feedback | same |

## Project overrides

Where a vendored skill conflicts with this repository, the constitution and
`AGENTS.md` win:

- **No suppressions to make a check pass** (Art. XI). The `gcc`,
  `static-analysis`, `sanitizers`, and `valgrind` skills show diagnostic
  pragmas, `NOLINT`, and suppression files. Here those are allowed only as
  their own declared change with a stated reason, never to turn a failure
  green.
- **Our build settings, not the examples':** C23, `-Wall -Wextra
  -Wpedantic -Werror`, the compilers in `baseline.md`, and the presets in
  `CMakePresets.json`. Examples showing C11 or ad-hoc flags are
  illustrations.
- **Humans merge:** a review verdict of "ready to merge" is advice. Agents
  never merge or approve (Art. XVI).
- **Cross-references:** "Related skills" lines point at upstream paths like
  `skills/build-systems/ninja`. Here every skill sits directly in
  `.agents/skills/<name>/`.
- **Scripts were not copied.** `systematic-debugging` mentions
  `find-polluter.sh` and a TypeScript example; neither applies here.

## Conventions shared by every skill

- **Writing to GitHub is outward-facing** (Art. XII). Build the issue or
  PR body locally, show the human, write only after a clear yes, then read
  it back. A yes covers the writes approved, in this session, for that
  issue.
- **Ownership.** Skills that change files or issue content work only on
  issues assigned to the human the agent works for (Art. XVI).
- **Task lines** use the Spec Kit format:
  `- [ ] T001 [P] Description naming the file (depends on T000)`. Once a
  task belongs to an issue, its line gains `→ #<number>`.
- **Never** set assignees or labels on your own initiative. Never merge,
  approve, or close anything.
- Skills are project-lifetime files: no specific issue numbers or dates.
