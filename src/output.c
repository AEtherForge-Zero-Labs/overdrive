#include "output.h"

#include <string.h>

void ovd_output_adopt(OvdOutput *output, const OvdAllocator *allocator, OvdBlock storage)
{
    ovd_alloc_release(allocator, &output->storage);
    output->storage = storage;
    output->length = 0;
}

void ovd_output_release(OvdOutput *output, const OvdAllocator *allocator)
{
    ovd_alloc_release(allocator, &output->storage);
    output->length = 0;
}

bool ovd_output_append(OvdOutput *output, const char *bytes, size_t length)
{
    if (length == 0) {
        return true;
    }
    /* length <= storage.size is the invariant, so this cannot wrap. */
    if (bytes == nullptr || length > output->storage.size - output->length) {
        return false;
    }
    char *const data = output->storage.data;
    memcpy(data + output->length, bytes, length);
    output->length += length;
    return true;
}

void ovd_output_reset(OvdOutput *output)
{
    output->length = 0;
}

size_t ovd_output_capacity(const OvdOutput *output)
{
    return output->storage.size;
}

size_t ovd_output_length(const OvdOutput *output)
{
    return output->length;
}

const char *ovd_output_bytes(const OvdOutput *output)
{
    return output->storage.data;
}
