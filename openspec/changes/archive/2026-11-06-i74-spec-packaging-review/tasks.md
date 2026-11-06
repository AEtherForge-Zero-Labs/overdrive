## 1. Defects

- [x] 1.1 D1: reword FR-003, and test it through `INTERFACE_COMPILE_OPTIONS`, `INTERFACE_COMPILE_DEFINITIONS`, and a diff of the cache (R202, T201).
- [x] 1.2 D2: add FR-016 and R212 (`OVERDRIVE_INSTALL`), move the license files to `doc/overdrive` (R207), and test that a vendored install adds nothing (T207).
- [x] 1.3 D3: declare a consumer target named `overdrive` unsupported, with its own guard message and test (FR-004, R203, T201, T202).

## 2. Review notes

- [x] 2.1 Use a target allowlist from `BUILDSYSTEM_TARGETS`, a SHA-256 check of the license files, the package registry off with an `overdrive_DIR` check, `CMAKE_TOOLCHAIN_FILE` forwarding, and a work directory per mode.
