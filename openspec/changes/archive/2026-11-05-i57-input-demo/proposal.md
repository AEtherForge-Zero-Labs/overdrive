## Why

Feature 002's SC-006 needs an input example that a person runs by hand, against a written checklist, in Windows Terminal and one POSIX terminal. Only a real terminal shows what a person sees: keys, mouse, paste, focus, resize, and the restore after a crash, a closed window, or a signal. This is OVD-20 (#57), tasks T121 and T122.

## What Changes

- `examples/input_demo.c` (T121), named from `OVD-IDENTITY-001` §8.3. It uses the public API only. It shows the last events:
  - keys with modifiers;
  - mouse action and button at a cell, with that cell marked;
  - paste length and first line;
  - focus;
  - size.
- It has options for mouse, paste, focus, a deliberate crash (`--crash=abort|segv`), and input off (`--no-input`). It quits on `q`.
- `examples/CMakeLists.txt` registers it.
- `docs/validation/manual-checklist.md` (T122) gains an `input_demo` part covering SC-006's steps, expected results, and a results template.
- `specs/002-input-restore/tasks.md`: T121 and T122 ticked.

## Impact

- No library code or public symbol changes.
- Real-terminal behavior stays UNVERIFIED until the owner runs the checklist. That run is a follow-up `needs-human` issue opened after merge, as for 001.
