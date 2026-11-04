/* The terminal and frame API (terminal.h, render.h), headless backend.
 * Owning module of struct OvdTerminal (internal.h). Composes the
 * allocator, the grid and canvas, the output buffer, and the diff.
 * research.md R21 to R25, R31, R35, R44. */
#include "internal.h"

#include "diff.h"

#include <overdrive/render.h>
#include <overdrive/terminal.h>

/* The blocks one size needs, in the order adopt takes them. */
static constexpr size_t STORAGE_BLOCKS = 3;

static bool size_positive(OvdSize size)
{
    return size.cols > 0 && size.rows > 0;
}

static bool size_equal(OvdSize a, OvdSize b)
{
    return a.cols == b.cols && a.rows == b.rows;
}

/* Two grids and an output sized to the worst frame (R30). Overflow in
 * any of them is OVD_ERROR_ALLOCATION, with no allocator call. */
static OvdResult storage_requests(OvdSize size, bool synchronized,
                                  OvdBlockRequest requests[STORAGE_BLOCKS])
{
    OvdBlockRequest grid;
    OvdResult result = ovd_buffer_request(size, &grid);
    if (result != OVD_OK) {
        return result;
    }
    size_t bound = 0;
    result = ovd_diff_bound(size, synchronized, &bound);
    if (result != OVD_OK) {
        return result;
    }
    requests[0] = grid;
    requests[1] = grid;
    requests[2] = (OvdBlockRequest){ .count = bound, .element_size = 1 };
    return OVD_OK;
}

/* Each owner takes its new block and releases its old one (R31). The new
 * size's first frame is a full redraw. */
static void adopt_storage(OvdTerminal *t, OvdBlock blocks[STORAGE_BLOCKS], OvdSize size)
{
    ovd_buffer_adopt(&t->grids[0], &t->allocator, &blocks[0], size);
    ovd_buffer_adopt(&t->grids[1], &t->allocator, &blocks[1], size);
    ovd_output_adopt(&t->output, &t->allocator, blocks[2]);
    blocks[2] = (OvdBlock){ nullptr, 0 };
    t->size = size;
    t->screen_known = false;
}

static OvdBuffer *frame_grid(OvdTerminal *t)
{
    return &t->grids[1 - t->screen];
}

OVD_API OvdTerminalConfig ovd_terminal_config_default(void)
{
    return (OvdTerminalConfig){
        .backend = OVD_BACKEND_SYSTEM,
        .detect_color_depth = true,
        .color_depth = OVD_COLOR_DEPTH_TRUECOLOR,
        .alternate_screen = true,
        .synchronized_output = false,
        .headless_size = { 80, 24 },
        .sink = nullptr,
        .sink_context = nullptr,
        .allocator = nullptr,
    };
}

OVD_API OvdResult ovd_terminal_create(const OvdTerminalConfig *config, OvdTerminal **out)
{
    if (config == nullptr || out == nullptr || config->backend > OVD_BACKEND_HEADLESS
        || config->color_depth > OVD_COLOR_DEPTH_TRUECOLOR) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    const OvdAllocator allocator
        = config->allocator != nullptr ? *config->allocator : ovd_alloc_default();
    if (!ovd_alloc_is_valid(&allocator)) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (config->backend == OVD_BACKEND_HEADLESS && !size_positive(config->headless_size)) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    /* The system backend arrives with OVD-11 (R44). */
    if (config->backend != OVD_BACKEND_HEADLESS) {
        return OVD_ERROR_UNSUPPORTED;
    }

    const OvdSize size = config->headless_size;
    OvdBlockRequest requests[1 + STORAGE_BLOCKS];
    requests[0] = (OvdBlockRequest){ .count = 1, .element_size = sizeof(OvdTerminal) };
    OvdResult result = storage_requests(size, config->synchronized_output, &requests[1]);
    if (result != OVD_OK) {
        return result;
    }
    OvdBlock blocks[1 + STORAGE_BLOCKS];
    result = ovd_alloc_all(&allocator, requests, 1 + STORAGE_BLOCKS, blocks);
    if (result != OVD_OK) {
        return result;
    }

    OvdTerminal *t = blocks[0].data;
    *t = (OvdTerminal){
        .allocator = allocator,
        .self = blocks[0],
        .backend = config->backend,
        .depth = config->color_depth, /* headless never detects (R35) */
        .synchronized = config->synchronized_output,
        .sink = config->sink,
        .sink_context = config->sink_context,
    };
    adopt_storage(t, &blocks[1], size);
    *out = t;
    return OVD_OK;
}

OVD_API void ovd_terminal_destroy(OvdTerminal *terminal)
{
    if (terminal == nullptr) {
        return;
    }
    ovd_frame_abandon(terminal);
    ovd_buffer_release(&terminal->grids[0], &terminal->allocator);
    ovd_buffer_release(&terminal->grids[1], &terminal->allocator);
    ovd_output_release(&terminal->output, &terminal->allocator);
    /* The struct lives in self: copy what releasing it needs first. */
    const OvdAllocator allocator = terminal->allocator;
    OvdBlock self = terminal->self;
    ovd_alloc_release(&allocator, &self);
}

OVD_API OvdSize ovd_terminal_size(const OvdTerminal *terminal)
{
    if (terminal == nullptr) {
        return (OvdSize){ 0, 0 };
    }
    return terminal->has_pending ? terminal->pending : terminal->size;
}

OVD_API OvdColorDepth ovd_terminal_color_depth(const OvdTerminal *terminal)
{
    return terminal == nullptr ? OVD_COLOR_DEPTH_NONE : terminal->depth;
}

OVD_API OvdResult ovd_terminal_set_size(OvdTerminal *terminal, OvdSize size)
{
    if (terminal == nullptr || !size_positive(size)) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (terminal->backend != OVD_BACKEND_HEADLESS) {
        return OVD_ERROR_UNSUPPORTED;
    }
    if (terminal->frame_open || terminal->sink_running) {
        return OVD_ERROR_STATE;
    }
    terminal->pending = size;
    terminal->has_pending = true;
    return OVD_OK;
}

OVD_API void ovd_terminal_invalidate(OvdTerminal *terminal)
{
    if (terminal == nullptr || terminal->sink_running) {
        return;
    }
    terminal->screen_known = false;
}

OVD_API OvdResult ovd_frame_begin(OvdTerminal *terminal, OvdCanvas **out)
{
    if (terminal == nullptr || out == nullptr) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (terminal->frame_open || terminal->sink_running) {
        return OVD_ERROR_STATE;
    }
    if (terminal->has_pending) {
        if (!size_equal(terminal->pending, terminal->size)) {
            /* Every new block before any old one is released (R31); a
             * refusal changes nothing and keeps the size pending (R21). */
            OvdBlockRequest requests[STORAGE_BLOCKS];
            OvdResult result
                = storage_requests(terminal->pending, terminal->synchronized, requests);
            if (result != OVD_OK) {
                return result;
            }
            OvdBlock blocks[STORAGE_BLOCKS];
            result = ovd_alloc_all(&terminal->allocator, requests, STORAGE_BLOCKS, blocks);
            if (result != OVD_OK) {
                return result;
            }
            adopt_storage(terminal, blocks, terminal->pending);
        }
        terminal->has_pending = false;
    }
    ovd_canvas_open(&terminal->canvas, frame_grid(terminal));
    terminal->frame_open = true;
    *out = &terminal->canvas;
    return OVD_OK;
}

OVD_API OvdResult ovd_frame_end(OvdTerminal *terminal)
{
    if (terminal == nullptr) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (terminal->sink_running || !terminal->frame_open) {
        return OVD_ERROR_STATE;
    }
    /* Closed before the sink runs, so calls from it see a closed canvas. */
    ovd_canvas_close(&terminal->canvas);
    terminal->frame_open = false;

    ovd_output_reset(&terminal->output);
    const OvdBuffer *screen = terminal->screen_known ? &terminal->grids[terminal->screen]
                                                     : nullptr;
    if (ovd_diff_render(screen, frame_grid(terminal), terminal->depth, terminal->synchronized,
                        &terminal->output)
        != OVD_OK) {
        /* Cannot happen within the R30 bound; if it does, nothing is
         * written and the next frame is a full redraw. */
        terminal->screen_known = false;
        return OVD_ERROR_IO;
    }
    const size_t length = ovd_output_length(&terminal->output);
    if (length > 0 && terminal->sink != nullptr) {
        terminal->sink_running = true;
        const bool delivered
            = terminal->sink(terminal->sink_context, ovd_output_bytes(&terminal->output), length);
        terminal->sink_running = false;
        if (!delivered) {
            terminal->screen_known = false;
            return OVD_ERROR_IO;
        }
    }
    terminal->screen = 1 - terminal->screen;
    terminal->screen_known = true;
    return OVD_OK;
}

OVD_API void ovd_frame_abandon(OvdTerminal *terminal)
{
    if (terminal == nullptr || terminal->sink_running || !terminal->frame_open) {
        return;
    }
    ovd_canvas_close(&terminal->canvas);
    terminal->frame_open = false;
}
