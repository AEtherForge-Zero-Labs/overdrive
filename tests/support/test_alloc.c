#include "test_alloc.h"

#include <stdlib.h>

static void record(TestAlloc *state, TestAllocEventKind kind, size_t size)
{
    if (state->event_count < TEST_ALLOC_MAX_EVENTS) {
        state->events[state->event_count] = (TestAllocEvent){ .kind = kind, .size = size };
        ++state->event_count;
    }
}

static void *test_allocate(void *context, size_t size)
{
    TestAlloc *state = context;
    ++state->allocate_calls;
    if (size == 0) {
        ++state->violations;
    }
    const bool refuse = state->refuse_all
                        || (state->refuse_call != 0 && state->allocate_calls == state->refuse_call);
    if (refuse) {
        record(state, TEST_ALLOC_EVENT_REFUSE, size);
        return nullptr;
    }

    size_t slot = 0;
    while (slot < TEST_ALLOC_MAX_BLOCKS && state->blocks[slot] != nullptr) {
        ++slot;
    }
    if (slot == TEST_ALLOC_MAX_BLOCKS) {
        /* The test outgrew the tracker: count it so the test fails. */
        ++state->violations;
        record(state, TEST_ALLOC_EVENT_REFUSE, size);
        return nullptr;
    }
    void *block = malloc(size == 0 ? 1 : size);
    if (block == nullptr) {
        record(state, TEST_ALLOC_EVENT_REFUSE, size);
        return nullptr;
    }
    state->blocks[slot] = block;
    state->sizes[slot] = size;
    ++state->live_blocks;
    state->live_bytes += size;
    record(state, TEST_ALLOC_EVENT_ALLOCATE, size);
    return block;
}

static void test_release(void *context, void *block, size_t size)
{
    TestAlloc *state = context;
    ++state->release_calls;
    record(state, TEST_ALLOC_EVENT_RELEASE, size);
    if (block == nullptr) {
        ++state->violations;
        return;
    }
    for (size_t slot = 0; slot < TEST_ALLOC_MAX_BLOCKS; ++slot) {
        if (state->blocks[slot] == block) {
            if (state->sizes[slot] != size) {
                ++state->violations;
            }
            --state->live_blocks;
            state->live_bytes -= state->sizes[slot];
            state->blocks[slot] = nullptr;
            state->sizes[slot] = 0;
            free(block);
            return;
        }
    }
    /* Not a block this allocator handed out: never free it. */
    ++state->violations;
}

void test_alloc_init(TestAlloc *state)
{
    *state = (TestAlloc){};
}

OvdAllocator test_alloc_allocator(TestAlloc *state)
{
    return (OvdAllocator){ .allocate = test_allocate, .release = test_release, .context = state };
}

void test_alloc_release_all(TestAlloc *state)
{
    for (size_t slot = 0; slot < TEST_ALLOC_MAX_BLOCKS; ++slot) {
        free(state->blocks[slot]);
        state->blocks[slot] = nullptr;
        state->sizes[slot] = 0;
    }
    state->live_blocks = 0;
    state->live_bytes = 0;
}
