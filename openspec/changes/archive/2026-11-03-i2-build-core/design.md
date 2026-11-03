## Context

See proposal.md for scope. The approved headers define the C23 boundary;
the machine baseline defines compiler and tool minimums. No sibling C
implementation or test infrastructure exists yet.

## Goals / Non-Goals

**Goals:** a warning-free core library, real failing-before-passing tests,
independent header compilation, and an export check that detects a
deliberately misnamed export.

**Non-Goals:** implementing the other declarations in the approved headers,
packaging/install rules, rendering, input, new agent infrastructure, or
changing the public contract.

## Decisions

- Keep one `overdrive` target and `overdrive::overdrive` alias. Honor
  `BUILD_SHARED_LIBS`; propagate only include paths, C23, and the shared
  ABI definition to consumers, not warning policy.
- Use the declared GCC/Clang compiler minimums and a compile-time C23
  feature probe. Reject unsupported compilers rather than silently
  accepting partial language support.
- Probe the C23 subset the public headers use, not every C23 feature.
  Clang's C implementation-status table reports object `constexpr` only
  from Clang 19, while the baseline allows Clang 18. The test's color
  input has no constant-expression requirement and uses `const`; no
  public contract is relaxed. Reference: https://clang.llvm.org/c_status.html.
- Declare warnings in one target helper, applied to library, tests,
  header translation units, and export fixtures. Release tests use
  explicit checks, not assertions removed by NDEBUG.
- Keep default compiler discovery in the shared presets, as the baseline
  requires; select GCC locally with CC and Clang through debug-clang.
  MinGW system/compiler/emulator settings live in the cross preset, so
  there is no extra toolchain file outside the issue's ownership.
- Put executable and DLL outputs together so native Windows and Wine
  tests can resolve a shared Overdrive without machine PATH changes.
- Generate per-header translation units and tiny export-fixture libraries
  in the build tree. Never commit generated C or build output.
- End generated C with a newline, including the C23 probe's bracket
  literal. Clang 18 CI caught trailing indentation after its last newline;
  the generator is corrected rather than suppressing `-Wnewline-eof`.
- Check defined external symbols for archives, dynamic exports for ELF,
  external defined symbols for Mach-O, and the PE export-name table.
  Remove Mach-O's ABI underscore only on Mach-O.
- A negative export test must fail specifically for its generic symbol,
  not merely for an arbitrary subprocess failure. A hidden generic helper
  in the good fixture checks that internal symbols are not mistaken for
  exports.
- Check public includes during configuration against public Overdrive
  headers and ISO C headers. Independently compiling each header catches
  missing prerequisites without relying on include order.
- Keep the two core functions transformational: neither retains state nor
  allocates. Unknown result values map to the documented static name.

## Boundaries and Assumptions

- Public API: the four copied headers, unchanged; only `ovd_version` and
  `ovd_result_name` acquire definitions here.
- Build inputs: compiler identity/version, C23 feature support, Ninja
  version, and cross-test emulator version are runtime-validated.
- Export listing: a tool failure or empty/unparseable listing fails closed.
- No additional library dependency. The cross preset uses the declared
  MinGW-w64 tools and Wine, not a downloaded SDK.
- Cross-platform CI is the evidence for Linux/Windows; local macOS checks
  do not establish runtime behavior on another operating system.
- Header copies remain explicitly marked experimental/proposed. Their
  declarations do not claim that later modules are implemented.

## Risks / Trade-offs

- Platform symbol tools have different formats → build positive/negative
  fixtures and exercise shared builds in CI as well as archives.
- Compiler minimums are provisional in the baseline → compile actual C23
  features before accepting a toolchain.
- Global personal Git ignore rules affect memory-check → reproduce its
  CI environment without changing repository rules or user settings.
- The existing landing guard remains defective → push a reviewed PR;
  never bypass it to merge.
