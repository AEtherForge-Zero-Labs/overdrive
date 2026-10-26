/*
 * Allocator dispatch (private). Every allocation Overdrive makes goes
 * through these functions, which enforce the OvdAllocator contract
 * (terminal.h, research.md R32) on the library's side:
 *
 * - allocate is never called with size 0 or with a size whose
 *   computation overflowed; such requests fail before any allocator call;
 * - release always receives the size that was requested, and never
 *   nullptr;
 * - a batch allocates every new block before the caller releases any old
 *   one, so a refused resize leaves the old blocks intact (R31).
 *
 * Nothing here reallocates (R31). Transformational: nothing outlives a
 * call; the allocator belongs to the caller.
 */
#ifndef OVERDRIVE_SRC_ALLOC_H
#define OVERDRIVE_SRC_ALLOC_H

#include <overdrive/core.h>
#include <overdrive/terminal.h>

#include <stddef.h>

/* A block and the size in bytes that was requested for it. Empty is
 * { nullptr, 0 }; otherwise data is non-null and size is positive. */
typedef struct OvdBlock {
    void *data;
    size_t size;
} OvdBlock;

/* One block request in a batch: count elements of element_size bytes. */
typedef struct OvdBlockRequest {
    size_t count;
    size_t element_size;
} OvdBlockRequest;

/* The allocator used when the configuration supplies none: malloc and
 * free, which return memory aligned for any object type (ISO C). */
OvdAllocator ovd_alloc_default(void);

/* True if allocator is non-null and both of its functions are non-null. */
bool ovd_alloc_is_valid(const OvdAllocator *allocator);

/* Allocates count * element_size bytes into *out.
 *
 * OVD_ERROR_INVALID_ARGUMENT: allocator invalid, out nullptr, or count or
 * element_size zero. OVD_ERROR_ALLOCATION: the product overflows size_t,
 * or the allocator returned nullptr. The first two make no allocator
 * call. On failure *out is unchanged. */
[[nodiscard]] OvdResult ovd_alloc_block(const OvdAllocator *allocator, size_t count,
                                        size_t element_size, OvdBlock *out);

/* Releases *block with its requested size and empties it. An empty block
 * makes no allocator call. Precondition: allocator is valid and is the
 * one that allocated the block. */
void ovd_alloc_release(const OvdAllocator *allocator, OvdBlock *block);

/* Allocates one block per request, all or nothing, into out[0 .. count).
 *
 * Every request's size is checked before the first allocator call, with
 * ovd_alloc_block's errors. Then the blocks are allocated in order; if
 * the allocator refuses one, the blocks already taken are released in
 * reverse order and OVD_ERROR_ALLOCATION is returned. On any failure
 * every out[i] is empty. count 0 succeeds without an allocator call.
 *
 * A resize calls this for all its new blocks, then gives each new block
 * to its owner, which releases the old one (R31). */
[[nodiscard]] OvdResult ovd_alloc_all(const OvdAllocator *allocator,
                                      const OvdBlockRequest *requests, size_t count,
                                      OvdBlock *out);

#endif /* OVERDRIVE_SRC_ALLOC_H */
