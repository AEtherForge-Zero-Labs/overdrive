# Quickstart: Validating Packaging

These are the commands that show the feature works. The contract is in
[contracts/consumer.md](contracts/consumer.md).

## Prerequisites

The machine baseline (`.specify/memory/baseline.md`): CMake 3.28+, Ninja
1.11+, and GCC 14, Clang 19, or Apple Clang 17. For the Windows cross
build, MinGW-w64 GCC 14 and Wine 10.

## 1. Both routes, through the test suite

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -R '^consumer_'
```

Expected: `consumer_vendored`, `consumer_duplicate`, and
`consumer_installed` pass. Repeat with
`shared`, and with `windows-cross-debug` and `windows-cross-shared`
where MinGW-w64 and Wine are installed. CI runs every preset in its
matrix.

## 2. The vendored route, by hand

From an empty directory, with a copy of Overdrive at
`third_party/overdrive`, a `CMakeLists.txt` that adds it and links
`overdrive::overdrive` (contract, Route 1), and a program that includes
`<overdrive/overdrive.h>`:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Expected: the build succeeds, the consumer's build tree holds no
Overdrive test, example, or benchmark target (`ninja -C build -t
targets all` lists none), and the program runs.

## 3. The installed route, by hand

```bash
cmake --install build/debug --prefix "$HOME/opt/overdrive"
cmake -S consumer -B consumer/build -G Ninja -DCMAKE_PREFIX_PATH="$HOME/opt/overdrive"
cmake --build consumer/build
```

Expected: the consumer builds and runs. The prefix holds only the files
in the contract's install layout.

## 4. A second copy is refused

`ctest --preset debug -R '^consumer_duplicate$'` adds Overdrive twice,
from two directories, to one test project. Expected: the test passes,
because configuration stopped with a message naming both directories.
