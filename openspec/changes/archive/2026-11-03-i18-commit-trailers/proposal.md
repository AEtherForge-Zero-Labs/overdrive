## Why

The commit template in `AGENTS.md` ended with one paragraph holding
`Refs #<number>`, `Assisted-by:`, and `Co-Authored-By:` (issue #18).
`Refs #<number>` has no colon, so it is not a trailer, and git parses a
mixed final paragraph as trailers only when one of them is git-generated
or configured. Neither is, so a commit written from the template carries
no trailers, `%(trailers:...)` is empty, and the guard blocks the push for
missing AI disclosure (Art. XVI).

## What Changes

- `AGENTS.md`, Commits: `Refs #<number>` moves to its own paragraph above
  the trailers, and a note says why and how to check a message with
  `git interpret-trailers --parse`.

## Capabilities

### New Capabilities

None. Requirements live in Spec Kit (constitution, preamble).

### Modified Capabilities

None in OpenSpec.

## Impact

Agent instructions only; no code, build, or public API changes. The
disclosure rule itself is unchanged: the fix makes the existing trailers
parse. This record was written after the first commit, when an
independent review found the transaction had none; it is not backdated.
