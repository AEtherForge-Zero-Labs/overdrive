## 1. Test

- [x] 1.1 Block `SIGHUP` before the signal in the mask subject, and require it to be blocked in the handler. Watch all three mask cases fail against the mutant that sets the mask to empty.

## 2. Fix

- [x] 2.1 In `call_previous`, block the additions first, then unblock the handled signals in neither `uc_sigmask` nor the additions.
- [x] 2.2 Record the order and its reason in R121, and reword its double parenthetical.

## 3. Delivery evidence

- [x] 3.1 Run `debug`, `sanitize`, `debug-clang`, `shared`, `windows-cross-debug`, and gcc-14 from clean with zero warnings.
