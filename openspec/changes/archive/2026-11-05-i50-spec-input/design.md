## Context

The feature's requirements, decisions, and contracts live in
`specs/002-input-restore/`; this change records how that directory was
produced and checked. 001's directory is the model for shape and
convention. Its decision numbers (R1–R48) are cited from code comments,
so 002 must not reuse them. `include/overdrive/` holds the implemented
001 headers and is not this change's to edit.

## Goals / Non-Goals

**Goals:** a feature directory that `speckit-tasks` can cut into
disjoint-file issues; every external API claim tagged by provenance;
proposed headers that compile under the project's flags.

**Non-Goals:** no library code; no edit to `include/overdrive/` or
001's directory; no GitHub issues for 002's tasks (they wait for the
owner's approval of `speckit-taskstoissues`).

## Decisions

**D1 — Decision numbers start at R101.** One namespace across features
keeps a bare "R26" unambiguous in code comments. *Rejected:* restarting
at R1 (collides with 001), feature-prefixed ids (no precedent).

**D2 — The proposed `terminal.h` is 001's implemented header plus
changes.** Copied from `include/overdrive/terminal.h`, so the delta
against what ships is exactly the new fields and comments, and the
implementing issue copies one file. *Rejected:* a fragment listing only
new fields: cannot be compiled.

**D3 — Spec edits found while planning are made in the spec, not only
in research.** The SIGWINCH handler (FR-036), the application-handler
rule (FR-032), and the normal-exit limit (FR-034) change observable
behavior, so they belong to the requirement text; research R111–R112
give the reasons.

**D4 — Provenance tagging in research.** Each external claim is
VERIFIED (read this session: POSIX §2.4.3's async-signal-safe list;
Microsoft's `SetConsoleMode` page), INFERRED, or ASSUMED. An ASSUMED
claim is a check the implementing issue owes before depending on it.
*Rejected:* verifying every xterm row now: the decode issue must quote
each into its test table anyway (R105), so doing it here duplicates it.

**D5 — Header proof.** Each proposed header compiles alone and twice
(include guard) under gcc-14 and clang with `-std=c23 -Wall -Wextra
-Wpedantic -Werror -Wconversion`, against 001's `core.h`, as the
i26-contract-nits change did.

## Risks / Trade-offs

- [ASSUMED Windows behavior: VT input through `ReadConsoleInputW`,
  console-control and exception-filter chaining] → named in research
  R107 and R113 as checks for the Windows issue and the manual checklist;
  a false assumption halts that issue (Art. XII).
- [Re-faulting after a crash handler returns is undefined in POSIX] →
  R112 names it; SC-003's pty test is the per-platform proof.
- [Process-wide state contradicts 001 FR-002] → the owner chose
  default-on; the exception is bounded and documented at the config
  fields (spec FR-036).
