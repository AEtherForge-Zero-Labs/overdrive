## Why

The independent review of `2026-11-06-i74-spec-packaging` failed it, with three defects. The reviewer's experiments ran on CMake 4.2.3.

- **D1. The FR-003 check could not fail.** It compared the consumer directory's variables before and after `add_subdirectory`, but a subdirectory's variables never reach its parent. The consumer's warning flags also equal Overdrive's, so a warning leak would be invisible anyway.
- **D2. A vendored Overdrive's install would write into the consumer's install.** `GNUInstallDirs` sets `CMAKE_INSTALL_DOCDIR` as an inherited normal variable, so the planned rules would put Overdrive's `LICENSE` over the consumer's. Unconditional install rules would also add Overdrive's headers and library.
- **D3. The edge case of a consumer target named `overdrive` had no resolution.**

The review also suggested:
- a target allowlist instead of a denylist;
- a SHA-256 check that `LICENSE` is installed unchanged;
- checking that `overdrive_DIR` is the moved prefix, with the package registry off;
- forwarding `CMAKE_TOOLCHAIN_FILE`;
- a separate work directory for each test mode.

## What Changes

- **FR-003** now names what linking may add. It is tested through the target's usage requirements and a diff of the cache.
- **New FR-016 and R212:** install rules sit behind `OVERDRIVE_INSTALL`, which defaults to `PROJECT_IS_TOP_LEVEL`. This is an assumption. A vendored install adds no Overdrive file, and a test checks that.
- **R207:** the license files go to `${CMAKE_INSTALL_DATAROOTDIR}/doc/overdrive`, never to `CMAKE_INSTALL_DOCDIR`.
- **R203 and FR-004:** a consumer target named `overdrive` is unsupported, and the guard names the collision.
- **Tests:** T201, T202, T203, T207, T208, and T209 take the review's checks. The contract, plan, data model, and work graph follow.

## Impact

Documentation only.
