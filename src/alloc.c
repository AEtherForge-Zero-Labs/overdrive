#include "alloc.h"

#include <stdckdint.h>
#include <stdlib.h>

/* The context is unused: the C library allocator has no state. */
static void *alloc_malloc(void *, size_t size)
{
    return malloc(size);
}

static void alloc_free(void *, void *block, size_t)
{
    free(block);
}

OvdAllocator ovd_alloc_default(void)
{
    return (OvdAllocator){ .allocate = alloc_malloc, .release = alloc_free, .context = nullptr };
}

bool ovd_alloc_is_valid(const OvdAllocator *allocator)
{
    return allocator != nullptr && allocator->allocate != nullptr && allocator->release != nullptr;
}

/* The byte size of a request, or an error; never calls the allocator. */
static OvdResult alloc_request_size(OvdBlockRequest request, size_t *size)
{
    if (request.count == 0 || request.element_size == 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (ckd_mul(size, request.count, request.element_size)) {
        return OVD_ERROR_ALLOCATION;
    }
    return OVD_OK;
}

OvdResult ovd_alloc_block(const OvdAllocator *allocator, size_t count, size_t element_size,
                          OvdBlock *out)
{
    if (!ovd_alloc_is_valid(allocator) || out == nullptr) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    size_t size = 0;
    const OvdResult sized = alloc_request_size(
        (OvdBlockRequest){ .count = count, .element_size = element_size }, &size);
    if (sized != OVD_OK) {
        return sized;
    }
    void *data = allocator->allocate(allocator->context, size);
    if (data == nullptr) {
        return OVD_ERROR_ALLOCATION;
    }
    *out = (OvdBlock){ .data = data, .size = size };
    return OVD_OK;
}

void ovd_alloc_release(const OvdAllocator *allocator, OvdBlock *block)
{
    if (block->data != nullptr) {
        allocator->release(allocator->context, block->data, block->size);
    }
    *block = (OvdBlock){};
}

OvdResult ovd_alloc_all(const OvdAllocator *allocator, const OvdBlockRequest *requests,
                        size_t count, OvdBlock *out)
{
    if (count == 0) {
        return OVD_OK;
    }
    if (!ovd_alloc_is_valid(allocator) || requests == nullptr || out == nullptr) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = (OvdBlock){};
    }
    /* Every size first, so a bad request makes no allocator call. */
    for (size_t i = 0; i < count; ++i) {
        size_t size = 0;
        const OvdResult sized = alloc_request_size(requests[i], &size);
        if (sized != OVD_OK) {
            return sized;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const OvdResult taken =
            ovd_alloc_block(allocator, requests[i].count, requests[i].element_size, &out[i]);
        if (taken != OVD_OK) {
            for (size_t j = i; j > 0; --j) {
                ovd_alloc_release(allocator, &out[j - 1]);
            }
            return taken;
        }
    }
    return OVD_OK;
}
