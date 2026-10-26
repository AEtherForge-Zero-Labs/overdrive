Closes #<!-- issue number -->

## What changed

<!-- One or two sentences. -->

## Why

<!-- The requirement or task this serves (spec FR/SC ids, task ids). -->

## Contract changes

<!-- Every public symbol added, changed, or removed, with its header; or "None".
     Transparent structs: why exposing the representation is intentional (Art. XIV). -->

## Evidence

<!-- The commands you ran and their actual output, or the decisive excerpt (Art. X).
     At least: configure, build, and ctest on the presets this change affects. -->

```text

```

**Unverified:** <!-- what was not run or observed, e.g. "not tested in Windows Terminal"; or "None" -->

## AI use

<!-- Which tools, what they did, and what the human reviewed and verified (Art. XVI). -->

## Checklist

- [ ] Links its issue; every issue it is blocked by is merged
- [ ] Only the issue's files changed (Art. IX)
- [ ] Exported symbols start with `ovd_`; types `Ovd`; constants `OVD_`; options `OVERDRIVE_` (Art. XIV)
- [ ] No consumer-specific types or names; no OS headers in public headers (Art. XIV)
- [ ] No new module, header, option, or dependency without a present requirement in the plan (Art. VII)
- [ ] No check weakened or suppressed (Art. XI)
- [ ] Documentation claims are implemented and measured where required (Art. XV)
- [ ] Every commit carries `Assisted-by:` where AI was used; no `Signed-off-by:` added by an agent
