---
name: "valgrind"
description: "Valgrind profiler skill for memory error detection and cache profiling. Use when running Memcheck to find heap corruption, use-after-free, memory leaks, or uninitialised reads; or Cachegrind/Callgrind for cache simulation and function-level profiling. Activates on queries about valgrind, memcheck, heap leaks, use-after-free without sanitizers, cachegrind, callgrind, KCachegrind, or massif memory profiling."
---

This is a pointer, so Claude Code can find the skill without a symlink
(symlinks break on Windows checkouts). The canonical skill, shared by every
agent tool, is:

**Read `.agents/skills/valgrind/SKILL.md` and follow it exactly, along with any
files it references in that directory. Pass along the user input below.**

```text
$ARGUMENTS
```
