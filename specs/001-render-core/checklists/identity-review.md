# Review Checklist: Identity and Architectural Separation

Run on every PR in feature 001 (reviewer agent and owner). Each item cites
its source. Mark ✅, ❌ (with file:line), or N/A.

## Naming (OVD-IDENTITY-001 §3, constitution Art. XIV)

- [ ] CHK001 Every new exported function starts with `ovd_` and is declared with `OVD_API` in a public header
- [ ] CHK002 Every new public type is `OvdPascalCase`; opaque types use `typedef struct OvdX OvdX;`
- [ ] CHK003 Every new public constant and enumerator is `OVD_UPPER_SNAKE_CASE`; enums have a fixed underlying type
- [ ] CHK004 Functions follow `ovd_<subsystem>_<operation>` with a consistent verb (create/destroy, begin/end, set/get-style accessors); no `ovd_do`/`_process`/`_handle`
- [ ] CHK005 New build options use `OVERDRIVE_`; the target stays `overdrive` / `overdrive::overdrive`
- [ ] CHK006 The exported-symbol check ran in CI and passed (FR-040)
- [ ] CHK007 Internal functions are `static` or declared only in private headers under `src/`

## Separation (Arts. IV, VII, XIV)

- [ ] CHK010 No OS header in any public header; OS headers only in `src/platform/` (FR-041)
- [ ] CHK011 No type, name, string, or behavior specific to a consuming application (no task, course, tasklib, PCC references) (FR-042)
- [ ] CHK012 Public headers include only public headers and the C standard library
- [ ] CHK013 Opaque types' fields are touched only by their owning module (plan module table)
- [ ] CHK014 No new module, header, option, function-pointer interface, or dependency beyond the plan's tables, or the plan was amended with a present requirement
- [ ] CHK015 No module from OVD-IDENTITY-001 §4 created without a spec requiring it
- [ ] CHK016 The PR touches only its issue's `owns` paths plus append-only lines in shared files

## Contracts (Arts. I, III)

- [ ] CHK020 `include/overdrive/` unchanged, or the PR is a contract-change issue approved by the owner
- [ ] CHK021 Runtime invariants are documented in the owning declaration's comment and tested
- [ ] CHK022 `[[nodiscard]]` on every fallible public function; no `(void)` casts to silence it (Art. XI)

## Claims (Art. XV, OVD-IDENTITY-001 §7.2)

- [ ] CHK030 Docs and README describe implemented capabilities as implemented and planned ones as planned
- [ ] CHK031 No §7.2 phrase (zero flicker, guaranteed FPS, universal compatibility, zero allocations, …) without cited measurement
- [ ] CHK032 Terminal compatibility statements name terminal, version, and platform
- [ ] CHK033 Product name is "Overdrive" in prose; tagline "Every frame matters." unaltered where used

## Security (Art. XIII)

- [ ] CHK040 No path from caller text to output bypasses the U+FFFD replacement
- [ ] CHK041 Every allocation and size computation is checked; writes are bounded
