---
name: "sanitizers"
description: "Compiler sanitizer skill for runtime bug detection in C/C++. Use when enabling and interpreting AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), ThreadSanitizer (TSan), MemorySanitizer (MSan), or LeakSanitizer (LSan) with GCC or Clang. Activates on queries about sanitizer flags, sanitizer reports, ASAN_OPTIONS, memory errors, data races, undefined behaviour, uninitialised reads, or choosing which sanitizer to use for a given bug class."
---

This is a pointer, so Claude Code can find the skill without a symlink
(symlinks break on Windows checkouts). The canonical skill, shared by every
agent tool, is:

**Read `.agents/skills/sanitizers/SKILL.md` and follow it exactly, along with any
files it references in that directory. Pass along the user input below.**

```text
$ARGUMENTS
```
