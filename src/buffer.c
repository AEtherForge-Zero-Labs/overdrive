#include "buffer.h"

#include <stdckdint.h>
#include <string.h>

static OvdCell *cells(const OvdBuffer *buffer)
{
    return buffer->storage.data;
}

static bool inside(const OvdBuffer *buffer, int32_t x, int32_t y)
{
    return x >= 0 && y >= 0 && x < buffer->size.cols && y < buffer->size.rows;
}

/* In bounds: the index fits, because cols x rows cells were allocated. */
static OvdCell *at(const OvdBuffer *buffer, int32_t x, int32_t y)
{
    return &cells(buffer)[(size_t)y * (size_t)buffer->size.cols + (size_t)x];
}

static void set_space(OvdCell *cell, OvdStyle style)
{
    memset(cell->text, 0, sizeof cell->text);
    cell->text[0] = ' ';
    cell->length = 1;
    cell->width = 1;
    cell->style = style;
}

/* Before (x, y) is overwritten: the other half of the wide grapheme it
 * belongs to, if any, becomes a space in its own style (R20). The
 * pairing invariant guarantees the partner exists. */
static void detach(const OvdBuffer *buffer, int32_t x, int32_t y)
{
    const OvdCell *cell = at(buffer, x, y);
    if (cell->width == 2) {
        OvdCell *tail = at(buffer, x + 1, y);
        set_space(tail, tail->style);
    } else if (cell->width == 0) {
        OvdCell *head = at(buffer, x - 1, y);
        set_space(head, head->style);
    }
}

OvdResult ovd_buffer_request(OvdSize size, OvdBlockRequest *out)
{
    if (out == nullptr || size.cols <= 0 || size.rows <= 0) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    size_t count;
    if (ckd_mul(&count, (size_t)size.cols, (size_t)size.rows)) {
        return OVD_ERROR_ALLOCATION;
    }
    *out = (OvdBlockRequest){ .count = count, .element_size = sizeof(OvdCell) };
    return OVD_OK;
}

void ovd_buffer_adopt(OvdBuffer *buffer, const OvdAllocator *allocator, OvdBlock *storage,
                      OvdSize size)
{
    ovd_alloc_release(allocator, &buffer->storage);
    buffer->storage = *storage;
    buffer->size = size;
    *storage = (OvdBlock){ nullptr, 0 };
    ovd_buffer_clear(buffer);
}

void ovd_buffer_release(OvdBuffer *buffer, const OvdAllocator *allocator)
{
    ovd_alloc_release(allocator, &buffer->storage);
    buffer->size = (OvdSize){ 0, 0 };
}

OvdSize ovd_buffer_size(const OvdBuffer *buffer)
{
    return buffer->size;
}

void ovd_buffer_clear(OvdBuffer *buffer)
{
    const size_t count = (size_t)buffer->size.cols * (size_t)buffer->size.rows;
    OvdCell *const grid = cells(buffer);
    for (size_t i = 0; i < count; ++i) {
        set_space(&grid[i], OVD_STYLE_DEFAULT);
    }
}

const OvdCell *ovd_buffer_cell(const OvdBuffer *buffer, int32_t x, int32_t y)
{
    return inside(buffer, x, y) ? at(buffer, x, y) : nullptr;
}

bool ovd_buffer_put(OvdBuffer *buffer, int32_t x, int32_t y, const OvdGrapheme *grapheme,
                    OvdStyle style)
{
    if (grapheme == nullptr || !inside(buffer, x, y) || grapheme->length == 0
        || grapheme->length > OVD_GRAPHEME_MAX_BYTES
        || (grapheme->width == 2 && x == buffer->size.cols - 1)) {
        return false;
    }
    const bool wide = grapheme->width == 2;
    detach(buffer, x, y);
    if (wide) {
        detach(buffer, x + 1, y);
    }
    OvdCell *const cell = at(buffer, x, y);
    memset(cell->text, 0, sizeof cell->text);
    memcpy(cell->text, grapheme->bytes, grapheme->length);
    cell->length = grapheme->length;
    cell->width = wide ? 2 : 1;
    cell->style = style;
    if (wide) {
        OvdCell *const tail = at(buffer, x + 1, y);
        memset(tail->text, 0, sizeof tail->text);
        tail->length = 0;
        tail->width = 0;
        tail->style = style;
    }
    return true;
}

bool ovd_buffer_put_space(OvdBuffer *buffer, int32_t x, int32_t y, OvdStyle style)
{
    if (!inside(buffer, x, y)) {
        return false;
    }
    detach(buffer, x, y);
    set_space(at(buffer, x, y), style);
    return true;
}
