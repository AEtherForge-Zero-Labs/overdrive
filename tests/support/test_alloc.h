/*
 * Counting, failing, and contract-checking allocator for tests.
 *
 * Wraps malloc and free. Counts calls, records them in order, can refuse
 * a chosen allocation, and counts every breach of the OvdAllocator
 * contract (terminal.h, research.md R32): allocate called with size 0,
 * release called with nullptr, with a block it did not hand out, or with
 * a size other than the one requested.
 */
#ifndef OVERDRIVE_TESTS_SUPPORT_TEST_ALLOC_H
#define OVERDRIVE_TESTS_SUPPORT_TEST_ALLOC_H

#include <overdrive/terminal.h>

#include <stddef.h>
#include <stdint.h>

/* An enum, not constexpr objects: Clang 18 (the baseline minimum) lacks
 * C23 object constexpr. */
enum : size_t {
    TEST_ALLOC_MAX_BLOCKS = 64,
    TEST_ALLOC_MAX_EVENTS = 64,
};

typedef enum TestAllocEventKind : uint8_t {
    TEST_ALLOC_EVENT_ALLOCATE,
    TEST_ALLOC_EVENT_REFUSE,
    TEST_ALLOC_EVENT_RELEASE,
} TestAllocEventKind;

typedef struct TestAllocEvent {
    TestAllocEventKind kind;
    size_t size;
} TestAllocEvent;

typedef struct TestAlloc {
    /* Configuration: refuse the allocate call with this 1-based number
     * (0 refuses none), or every call when refuse_all is set. */
    size_t refuse_call;
    bool refuse_all;

    /* Observations. */
    size_t allocate_calls; /* includes refused calls */
    size_t release_calls;
    size_t live_blocks;
    size_t live_bytes;
    size_t violations;
    TestAllocEvent events[TEST_ALLOC_MAX_EVENTS];
    size_t event_count; /* saturates at TEST_ALLOC_MAX_EVENTS */

    void *blocks[TEST_ALLOC_MAX_BLOCKS];
    size_t sizes[TEST_ALLOC_MAX_BLOCKS];
} TestAlloc;

/* Resets every counter and setting. */
void test_alloc_init(TestAlloc *state);

/* An OvdAllocator whose context is state. */
OvdAllocator test_alloc_allocator(TestAlloc *state);

/* Releases every live block (for cleanup after a failed check). */
void test_alloc_release_all(TestAlloc *state);

#endif /* OVERDRIVE_TESTS_SUPPORT_TEST_ALLOC_H */
