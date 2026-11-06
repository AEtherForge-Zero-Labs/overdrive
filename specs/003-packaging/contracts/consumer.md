# Contract: Consuming Overdrive from CMake

This is the build interface a consumer project relies on. Changing
anything in it is a contract change, like a change to a public header
(Arts. I, XIV). The C API is in `include/overdrive/` and does not
change in this feature.

## Target

| Name | Kind | Carries |
|---|---|---|
| `overdrive::overdrive` | library: static, or shared when `BUILD_SHARED_LIBS` is on | the public include directory, the C23 requirement, `OVERDRIVE_SHARED` when shared, its link dependencies |

The unprefixed name `overdrive` is internal, and consumers do not use
it. Both routes give the same target with the same usage requirements.

## Route 1: vendored subproject

```cmake
add_subdirectory(third_party/overdrive)          # or a path and a binary dir
target_link_libraries(app PRIVATE overdrive::overdrive)
```

- **Requirements**: CMake 3.28; GCC 14, Clang 19, or Apple Clang 17.
  Any other compiler stops configuration with Overdrive's existing
  message.
- **Defaults when not top-level**: tests, examples, benchmarks, and
  install rules are off. A consumer turns one on by setting its option
  before adding the directory: `OVERDRIVE_BUILD_TESTS`,
  `OVERDRIVE_BUILD_EXAMPLES`, `OVERDRIVE_BUILD_BENCHMARKS`, or
  `OVERDRIVE_INSTALL`.
- **What linking adds**: C23 and, when shared, `OVERDRIVE_SHARED`, on
  targets that link `overdrive::overdrive`. Nothing else: no compile
  options, no other definitions.
- **Not changed in the consumer's build**: targets that do not link
  Overdrive, whether testing is enabled, and cache entries outside
  `OVERDRIVE_*`, `overdrive_*`, and `BUILD_SHARED_LIBS`.
- **A second copy** in the same build stops configuration with a message
  naming both source directories.
- **A consumer target named `overdrive`** is unsupported. Configuration
  stops with a message naming the collision.
- **Fetching at configure time** (for example with `FetchContent`) is
  this route.

## Route 2: installed package

```cmake
find_package(overdrive 0.1 REQUIRED)             # from CMAKE_PREFIX_PATH or overdrive_DIR
target_link_libraries(app PRIVATE overdrive::overdrive)
```

- **Package name**: `overdrive`. The files are `overdrive-config.cmake`
  and `overdrive-config-version.cmake`, under
  `<prefix>/<libdir>/cmake/overdrive/`.
- **Version check**: while the major version is 0, a request is
  satisfied only by the same major and minor version. A request with no
  version accepts any installed version.
- **Relocatable**: the prefix can be moved after installing.

## Install layout

This is the install of a top-level Overdrive build, or of a vendored
one with `OVERDRIVE_INSTALL` on. It holds exactly these files, and
anything else is a defect (FR-010).

| Path under the prefix | Contents |
|---|---|
| `include/overdrive/{core,terminal,render,input,widgets,overdrive}.h` | the public headers |
| `<libdir>/` | the static library, or on POSIX the shared library and its version links |
| `bin/` (Windows, shared) | the DLL |
| `<libdir>/` (Windows, shared) | the import library |
| `<libdir>/cmake/overdrive/` | the config, version, and targets files |
| `<datarootdir>/doc/overdrive/LICENSE` | Overdrive's `LICENSE`, byte-for-byte unchanged |
| `<datarootdir>/doc/overdrive/LICENSE-UCD` | the notice for the vendored Unicode data compiled into the library |

`<libdir>` and `<datarootdir>` are the `GNUInstallDirs` defaults unless
the installer overrides them. The documentation directory is always
`doc/overdrive`, never the consumer's `CMAKE_INSTALL_DOCDIR`.

## Windows shared libraries

A program linking a shared Overdrive needs the DLL beside it or on
`PATH`. The recommended step copies it after each build:

```cmake
if(WIN32)
    add_custom_command(TARGET app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t
            $<TARGET_FILE_DIR:app> $<TARGET_RUNTIME_DLLS:app>
        COMMAND_EXPAND_LISTS)
endif()
```

With a static Overdrive the DLL list is empty, and this form copies
nothing. `copy_if_different` with an empty list fails instead (R209).
