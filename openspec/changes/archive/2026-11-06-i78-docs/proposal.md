## Why

Feature 003's two routes are implemented and proved (OVD-22 and OVD-23).
Consumers still read the README's old advice, which says there are no
install rules and tells them to switch the tests off by hand. This change
documents both routes as the consumer tests show them (OVD-24, tasks
T212 and T213, FR-015).

## What Changes

- `README.md`:
  - a "Using Overdrive" section: both routes, the vendored defaults, the
    0.x version rule, the Windows DLL step, and a link to the contract;
  - the old subdirectory advice removed;
  - a Status sentence that contradicts the recorded terminal observations
    corrected.
- `docs/architecture.md`:
  - a "Packaging" section: the routes, the header file set as the one
    list, and the consumer tests;
  - the status banner updated.
- `docs/compatibility.md`: the consumer tests added to "What these runs
  show".

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None.

## Impact

Documentation only: `README.md`, `docs/architecture.md`, and
`docs/compatibility.md`.
