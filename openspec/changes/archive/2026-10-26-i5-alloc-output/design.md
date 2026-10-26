## Context

The issue body predates the contract resolution and says the output
buffer "grows geometrically". `research.md` R30 to R32 and `tasks.md`
T016/T017 on main replace that: the output buffer is allocated once per
terminal size to a proven worst-case bound, refuses appends past it, and
nothing reallocates (`OvdAllocator` has only `allocate`, `release`, and
`context`). This change follows R30 to R32; the issue's wording is not
implemented.

## Goals / Non-Goals

**Goals:** allocator dispatch that never calls `allocate` with size 0 or
with an overflowed size, always gives `release` the requested size, and
never calls `release` with nullptr; an all-or-nothing batch so a resize
can allocate every new block before releasing any old one; an output
buffer with bounded, all-or-nothing appends and reusable capacity.

**Non-Goals:** computing the R30 bound (the diff module's job, from the
encodings it emits), the terminal's resize sequence, growth of any kind.

## Decisions

- `OvdBlock { data, size }` carries a block with its requested byte size,
  so `release` always receives the size `allocate` was asked for (R32)
  without callers recomputing a product.
- Sizes are `count × element size`, checked with C23 `ckd_mul`. Overflow
  is `OVD_ERROR_ALLOCATION` with no allocator call (R32). A zero count or
  element size is `OVD_ERROR_INVALID_ARGUMENT` with no allocator call: it
  is a caller bug, and `allocate` must never see size 0.
- `ovd_alloc_all` checks every size before the first allocator call, then
  allocates in order; on a refusal it releases what it took, in reverse,
  and leaves every output block empty. The caller then hands each new
  block to its owner, which releases the old one. That is R31's
  allocate-then-release without a function-pointer interface.
- `OvdOutput` never allocates. It adopts a block (releasing any previous
  one), which fixes its capacity until the next adoption. The struct is
  declared in the private header so the terminal can embed it without a
  separate allocation; only `output.c` changes its fields (Art. IV).
- Appends are all-or-nothing: an append that does not fit writes nothing
  and returns false (R30: the frame then fails with `OVD_ERROR_IO`, which
  is the terminal's decision, not this module's).
- The default allocator wraps `malloc` and `free`; `malloc` returns
  memory aligned for any object type (ISO C 7.24.3), as R32 requires.
- Tests use the library's internal functions in every preset. In a
  shared build those are hidden, so `test_alloc` compiles `src/alloc.c`
  and `src/output.c` itself; the library also compiles them, which keeps
  them under the library's flags and the export check.

## Paradigm (Art. IV)

- `alloc`: transformational. Nothing outlives a call; the allocator is
  the caller's.
- `output`: owning module of `OvdOutput` (invariant `length <= capacity`).

## Risks / Trade-offs

- A test that compiles internal sources and links the static archive
  could see duplicate definitions if a future archive member it pulls in
  also needs `alloc.o`. Today nothing does; a later issue that hits it
  should introduce an internal object library through its own plan.
