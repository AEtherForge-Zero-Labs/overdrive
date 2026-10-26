/* Allocator dispatch and output buffer (T016; research.md R30-R32). */
#include "alloc.h"
#include "output.h"
#include "support/test_alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static bool event_is(const TestAlloc *state, size_t index, TestAllocEventKind kind, size_t size)
{
    return index < state->event_count && state->events[index].kind == kind
           && state->events[index].size == size;
}

static bool block_is_empty(OvdBlock block)
{
    return block.data == nullptr && block.size == 0;
}

static bool test_validation(void)
{
    const OvdAllocator fallback = ovd_alloc_default();
    CHECK(ovd_alloc_is_valid(&fallback));
    CHECK(!ovd_alloc_is_valid(nullptr));

    TestAlloc state;
    test_alloc_init(&state);
    OvdAllocator missing = test_alloc_allocator(&state);
    missing.allocate = nullptr;
    CHECK(!ovd_alloc_is_valid(&missing));
    missing = test_alloc_allocator(&state);
    missing.release = nullptr;
    CHECK(!ovd_alloc_is_valid(&missing));

    const OvdBlock sentinel = { .data = &state, .size = 7 };
    OvdBlock block = sentinel;
    CHECK(ovd_alloc_block(&missing, 1, 1, &block) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_alloc_block(nullptr, 1, 1, &block) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(block.data == sentinel.data && block.size == sentinel.size);
    CHECK(state.allocate_calls == 0);
    return true;
}

static bool test_default_allocator(void)
{
    const OvdAllocator fallback = ovd_alloc_default();
    OvdBlock block = {};
    CHECK(ovd_alloc_block(&fallback, 16, sizeof(uint64_t), &block) == OVD_OK);
    CHECK(block.data != nullptr);
    CHECK(block.size == 16 * sizeof(uint64_t));
    memset(block.data, 0xA5, block.size);
    ovd_alloc_release(&fallback, &block);
    CHECK(block_is_empty(block));
    return true;
}

static bool test_block_sizes(void)
{
    TestAlloc state;
    test_alloc_init(&state);
    const OvdAllocator allocator = test_alloc_allocator(&state);
    const OvdBlock sentinel = { .data = &state, .size = 3 };
    OvdBlock block = sentinel;

    /* Size 0 is rejected before the allocator sees it (R32). */
    CHECK(ovd_alloc_block(&allocator, 0, 8, &block) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_alloc_block(&allocator, 8, 0, &block) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_alloc_block(&allocator, 1, 1, nullptr) == OVD_ERROR_INVALID_ARGUMENT);

    /* Overflow is OVD_ERROR_ALLOCATION with no allocator call (R32). */
    CHECK(ovd_alloc_block(&allocator, SIZE_MAX, 2, &block) == OVD_ERROR_ALLOCATION);
    CHECK(ovd_alloc_block(&allocator, SIZE_MAX / 2 + 1, 2, &block) == OVD_ERROR_ALLOCATION);
    CHECK(ovd_alloc_block(&allocator, 2, SIZE_MAX / 2 + 1, &block) == OVD_ERROR_ALLOCATION);
    CHECK(block.data == sentinel.data && block.size == sentinel.size);
    CHECK(state.allocate_calls == 0);

    /* The largest product that fits is passed through, not rejected. */
    state.refuse_all = true;
    CHECK(ovd_alloc_block(&allocator, SIZE_MAX, 1, &block) == OVD_ERROR_ALLOCATION);
    CHECK(state.allocate_calls == 1);
    CHECK(event_is(&state, 0, TEST_ALLOC_EVENT_REFUSE, SIZE_MAX));
    CHECK(block.data == sentinel.data && block.size == sentinel.size);
    state.refuse_all = false;

    /* Success: the requested size is recorded and released exactly. */
    CHECK(ovd_alloc_block(&allocator, 3, 5, &block) == OVD_OK);
    CHECK(block.data != nullptr && block.size == 15);
    CHECK(state.live_blocks == 1 && state.live_bytes == 15);
    ovd_alloc_release(&allocator, &block);
    CHECK(block_is_empty(block));
    CHECK(state.release_calls == 1);
    CHECK(event_is(&state, 2, TEST_ALLOC_EVENT_RELEASE, 15));

    /* Releasing an empty block never calls release with nullptr. */
    ovd_alloc_release(&allocator, &block);
    CHECK(state.release_calls == 1);
    CHECK(state.live_blocks == 0 && state.violations == 0);
    return true;
}

static bool test_batch(void)
{
    TestAlloc state;
    test_alloc_init(&state);
    const OvdAllocator allocator = test_alloc_allocator(&state);
    const OvdBlockRequest requests[] = {
        { .count = 4, .element_size = 8 },
        { .count = 2, .element_size = 3 },
        { .count = 10, .element_size = 1 },
    };
    OvdBlock out[3] = {};

    CHECK(ovd_alloc_all(&allocator, requests, 0, out) == OVD_OK);
    CHECK(state.allocate_calls == 0);

    CHECK(ovd_alloc_all(&allocator, requests, 3, out) == OVD_OK);
    CHECK(out[0].size == 32 && out[1].size == 6 && out[2].size == 10);
    CHECK(event_is(&state, 0, TEST_ALLOC_EVENT_ALLOCATE, 32));
    CHECK(event_is(&state, 1, TEST_ALLOC_EVENT_ALLOCATE, 6));
    CHECK(event_is(&state, 2, TEST_ALLOC_EVENT_ALLOCATE, 10));
    for (size_t i = 0; i < 3; ++i) {
        ovd_alloc_release(&allocator, &out[i]);
    }
    CHECK(state.live_blocks == 0);

    /* An overflow or zero anywhere fails before the first allocator call. */
    test_alloc_init(&state);
    const OvdBlockRequest overflow_last[] = {
        { .count = 4, .element_size = 8 },
        { .count = SIZE_MAX, .element_size = SIZE_MAX },
    };
    CHECK(ovd_alloc_all(&allocator, overflow_last, 2, out) == OVD_ERROR_ALLOCATION);
    CHECK(block_is_empty(out[0]) && block_is_empty(out[1]));
    const OvdBlockRequest zero_middle[] = {
        { .count = 4, .element_size = 8 },
        { .count = 0, .element_size = 8 },
        { .count = 1, .element_size = 1 },
    };
    CHECK(ovd_alloc_all(&allocator, zero_middle, 3, out) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_alloc_all(&allocator, nullptr, 1, out) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_alloc_all(&allocator, requests, 1, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(state.allocate_calls == 0);

    /* A refusal releases what was taken, in reverse, and empties out. */
    test_alloc_init(&state);
    state.refuse_call = 3;
    CHECK(ovd_alloc_all(&allocator, requests, 3, out) == OVD_ERROR_ALLOCATION);
    CHECK(state.event_count == 5);
    CHECK(event_is(&state, 0, TEST_ALLOC_EVENT_ALLOCATE, 32));
    CHECK(event_is(&state, 1, TEST_ALLOC_EVENT_ALLOCATE, 6));
    CHECK(event_is(&state, 2, TEST_ALLOC_EVENT_REFUSE, 10));
    CHECK(event_is(&state, 3, TEST_ALLOC_EVENT_RELEASE, 6));
    CHECK(event_is(&state, 4, TEST_ALLOC_EVENT_RELEASE, 32));
    for (size_t i = 0; i < 3; ++i) {
        CHECK(block_is_empty(out[i]));
    }
    CHECK(state.live_blocks == 0 && state.violations == 0);

    test_alloc_init(&state);
    state.refuse_call = 1;
    CHECK(ovd_alloc_all(&allocator, requests, 3, out) == OVD_ERROR_ALLOCATION);
    CHECK(state.event_count == 1 && state.release_calls == 0);
    return true;
}

static bool test_output(void)
{
    TestAlloc state;
    test_alloc_init(&state);
    const OvdAllocator allocator = test_alloc_allocator(&state);

    /* Zero-initialized: capacity 0, refuses any byte. */
    OvdOutput output = {};
    CHECK(ovd_output_capacity(&output) == 0);
    CHECK(ovd_output_length(&output) == 0);
    CHECK(ovd_output_bytes(&output) == nullptr);
    CHECK(ovd_output_append(&output, nullptr, 0));
    CHECK(!ovd_output_append(&output, "x", 1));

    OvdBlock storage = {};
    CHECK(ovd_alloc_block(&allocator, 8, 1, &storage) == OVD_OK);
    ovd_output_adopt(&output, &allocator, storage);
    CHECK(ovd_output_capacity(&output) == 8);
    CHECK(ovd_output_length(&output) == 0);
    const char *const bytes = ovd_output_bytes(&output);
    CHECK(bytes != nullptr);
    const size_t calls_before = state.allocate_calls + state.release_calls;

    CHECK(ovd_output_append(&output, "abc", 3));
    CHECK(ovd_output_length(&output) == 3);
    /* Past capacity: refused, nothing written (R30). */
    CHECK(!ovd_output_append(&output, "DEFGHI", 6));
    CHECK(ovd_output_length(&output) == 3);
    CHECK(!ovd_output_append(&output, "Z", SIZE_MAX));
    CHECK(!ovd_output_append(&output, nullptr, 1));
    CHECK(ovd_output_length(&output) == 3);
    /* Exactly to capacity, then nothing more. */
    CHECK(ovd_output_append(&output, "defgh", 5));
    CHECK(ovd_output_length(&output) == 8);
    CHECK(memcmp(ovd_output_bytes(&output), "abcdefgh", 8) == 0);
    CHECK(!ovd_output_append(&output, "i", 1));
    CHECK(ovd_output_append(&output, "", 0));
    CHECK(memcmp(ovd_output_bytes(&output), "abcdefgh", 8) == 0);

    /* Reset reuses the same storage. */
    ovd_output_reset(&output);
    CHECK(ovd_output_length(&output) == 0);
    CHECK(ovd_output_capacity(&output) == 8);
    CHECK(ovd_output_bytes(&output) == bytes);
    CHECK(ovd_output_append(&output, "12345678", 8));
    CHECK(memcmp(ovd_output_bytes(&output), "12345678", 8) == 0);
    /* Appending and resetting never touch the allocator. */
    CHECK(state.allocate_calls + state.release_calls == calls_before);

    /* Adopting new storage releases the old with its requested size. */
    OvdBlock larger = {};
    CHECK(ovd_alloc_block(&allocator, 32, 1, &larger) == OVD_OK);
    ovd_output_adopt(&output, &allocator, larger);
    CHECK(ovd_output_capacity(&output) == 32 && ovd_output_length(&output) == 0);
    CHECK(event_is(&state, state.event_count - 1, TEST_ALLOC_EVENT_RELEASE, 8));
    CHECK(state.live_blocks == 1);

    ovd_output_release(&output, &allocator);
    CHECK(ovd_output_capacity(&output) == 0 && ovd_output_length(&output) == 0);
    CHECK(ovd_output_bytes(&output) == nullptr);
    CHECK(state.live_blocks == 0);
    ovd_output_release(&output, &allocator);
    CHECK(state.violations == 0);
    return true;
}

/* A resize in the shape R31 requires: every new block first, then each
 * owner adopts its new block and releases the old one. */
static bool test_resize(void)
{
    TestAlloc state;
    test_alloc_init(&state);
    const OvdAllocator allocator = test_alloc_allocator(&state);

    OvdOutput output = {};
    OvdBlock cells = {};
    OvdBlock first = {};
    CHECK(ovd_alloc_block(&allocator, 16, 1, &first) == OVD_OK);
    ovd_output_adopt(&output, &allocator, first);
    CHECK(ovd_alloc_block(&allocator, 4, 4, &cells) == OVD_OK);
    CHECK(ovd_output_append(&output, "kept", 4));

    const OvdBlockRequest requests[] = {
        { .count = 9, .element_size = 4 }, /* cells */
        { .count = 64, .element_size = 1 }, /* output */
    };
    OvdBlock fresh[2] = {};

    /* Refused: the old blocks and the output's contents survive. */
    state.refuse_call = state.allocate_calls + 2;
    CHECK(ovd_alloc_all(&allocator, requests, 2, fresh) == OVD_ERROR_ALLOCATION);
    CHECK(ovd_output_capacity(&output) == 16);
    CHECK(ovd_output_length(&output) == 4);
    CHECK(memcmp(ovd_output_bytes(&output), "kept", 4) == 0);
    CHECK(cells.data != nullptr && cells.size == 16);
    CHECK(state.live_blocks == 2);

    /* Accepted: both allocations precede both releases. */
    state.refuse_call = 0;
    const size_t start = state.event_count;
    CHECK(ovd_alloc_all(&allocator, requests, 2, fresh) == OVD_OK);
    ovd_alloc_release(&allocator, &cells);
    cells = fresh[0];
    ovd_output_adopt(&output, &allocator, fresh[1]);
    CHECK(state.event_count == start + 4);
    CHECK(event_is(&state, start + 0, TEST_ALLOC_EVENT_ALLOCATE, 36));
    CHECK(event_is(&state, start + 1, TEST_ALLOC_EVENT_ALLOCATE, 64));
    CHECK(event_is(&state, start + 2, TEST_ALLOC_EVENT_RELEASE, 16));
    CHECK(event_is(&state, start + 3, TEST_ALLOC_EVENT_RELEASE, 16));
    CHECK(ovd_output_capacity(&output) == 64);

    ovd_alloc_release(&allocator, &cells);
    ovd_output_release(&output, &allocator);
    CHECK(state.live_blocks == 0 && state.violations == 0);
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "validation", test_validation },
        { "default allocator", test_default_allocator },
        { "block sizes", test_block_sizes },
        { "batch", test_batch },
        { "output", test_output },
        { "resize", test_resize },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (tests[i].run()) {
            printf("ok   %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
