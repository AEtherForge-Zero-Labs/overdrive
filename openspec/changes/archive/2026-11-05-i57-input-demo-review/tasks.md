## 1. Example

- [x] 1.1 Replace `--no-input`'s clock loop with a sleep of about 100 ms. First, watch the pty smoke check "CPU under 0.2 s in 2 s" fail: it measured 2.00 s.

## 2. Checklist

- [x] 2.1 Make every quoted key line in part 2 match the demo's output, and check each one exactly in the pty smoke run.
- [x] 2.2 Add the review's notes to the checklist: the waiting bullet for `--no-input`, no `sanitize` build, paste text without `q` or `c`, and `Ctrl+j` for a pasted line feed.

## 3. Delivery evidence

- [x] 3.1 Run `debug`, `sanitize`, `debug-clang`, `shared`, `windows-cross-debug`, and gcc-14 `release` from clean with zero warnings.
