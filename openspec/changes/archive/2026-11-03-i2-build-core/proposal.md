## Why

Issue #2 is the first executable increment of feature 001. There is no
supported build or library yet, so none of the rendering issues can start.
The approved contracts from #21 are now on main.

## What Changes

- Add the preset-driven C23 library build, strict compiler checks, static
  and shared variants, and a MinGW/Wine cross-build.
- Copy the four approved headers unchanged into the public include tree.
- Implement runtime version reporting and result names, with tests.
- Compile every public header independently and check its include boundary.
- Check exported symbols with positive and negative library fixtures.
- Run the same presets in compiler/platform CI, with a `ci-ok` aggregate.

## Capabilities

### New Capabilities

None in OpenSpec. Requirements remain in Spec Kit; this transaction
implements T001–T009 without changing their requirements.

### Modified Capabilities

None.

## Impact

Only the paths owned by issue #2 and this transaction record change.
No rendering, terminal lifecycle, installation, or harness work is added.
The remaining contract clarifications in #26 precede the implementation
of the affected terminal behavior; this increment implements only core.
