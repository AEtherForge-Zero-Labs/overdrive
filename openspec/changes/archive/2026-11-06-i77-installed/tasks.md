## 1. Tests first

- [x] 1.1 Route `installed` in the consumer, mode `installed` and the FR-016 step in `run_consumer.cmake`, and `consumer_installed` with `TIMEOUT 300`. Watch it fail with no install rules, and watch FR-016 fail with `OVERDRIVE_INSTALL` forced on (T207).

## 2. Build changes

- [x] 2.1 `OVERDRIVE_INSTALL`, then the install, export, config, and version rules (T208).
- [x] 2.2 `LICENSE` and `LICENSE-UCD` to `${CMAKE_INSTALL_DATAROOTDIR}/doc/overdrive` (T209).

## 3. Proof

- [x] 3.1 Drop a header from the file set and watch `consumer_installed` fail; restore it (T210).
- [x] 3.2 Every T206 preset from clean, with 0 warnings and the three consumer tests passing (T211). The PR's CI run, with the same tests in every job, is the merge gate and is cited on the PR.
- [x] 3.3 Check the ASSUMED items R205 (lowercase config name), R206 (prefix-relative targets file), and R208 (`TIMEOUT` precedence) against the CMake documentation.
