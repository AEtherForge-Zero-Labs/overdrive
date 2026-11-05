/* The input API (input.h): ovd_input_wait and ovd_input_feed over the
 * decoder (decode.h). One file of the terminal module, which owns the
 * input state (internal.h). The headless backend's bytes come from feed
 * and its time is a virtual clock. research.md R102, R103, R109, R110,
 * R114. */
#include "internal.h"

#include <overdrive/input.h>
#include <overdrive/terminal.h>

#include <stdint.h>
#include <string.h>

bool ovd_input_adopt(OvdInput *input, OvdBlock queue, OvdBlock text, uint32_t escape_delay_ms)
{
    *input = (OvdInput){ .queue = queue, .text = text };
    return ovd_decoder_init(&input->decoder, escape_delay_ms, text.data, text.size);
}

void ovd_input_release(OvdInput *input, const OvdAllocator *allocator)
{
    ovd_alloc_release(allocator, &input->text);
    ovd_alloc_release(allocator, &input->queue);
    input->start = 0;
    input->length = 0;
}

/* clock + timeout_ms, saturating; a negative timeout adds nothing. */
static uint64_t clock_after(uint64_t clock_ms, int32_t timeout_ms)
{
    const uint64_t step = timeout_ms > 0 ? (uint64_t)timeout_ms : 0;
    return clock_ms > UINT64_MAX - step ? UINT64_MAX : clock_ms + step;
}

/* Decodes queued bytes at the current clock until an event is produced
 * or the queue is empty. With an empty queue, one call still lets held
 * bytes whose deadline has passed resolve. The decoder consumes at least
 * one byte or returns an event per call, so the loop ends. */
static bool next_event(OvdInput *input, OvdEvent *event)
{
    for (;;) {
        const char *bytes = input->length > 0 ? (const char *)input->queue.data + input->start
                                              : nullptr;
        size_t consumed = 0;
        const bool produced = ovd_decoder_next(&input->decoder, bytes, input->length,
                                               input->clock_ms, &consumed, event);
        input->start += consumed;
        input->length -= consumed;
        if (input->length == 0) {
            input->start = 0;
        }
        if (produced) {
            return true;
        }
        if (input->length == 0) {
            return false;
        }
    }
}

OVD_API OvdResult ovd_input_wait(OvdTerminal *terminal, int32_t timeout_ms, OvdEvent *event)
{
    if (terminal == nullptr || event == nullptr) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (!terminal->input_on || terminal->sink_running) {
        return OVD_ERROR_STATE;
    }
    OvdInput *input = &terminal->input;

    /* A stored size first, so a full queue cannot delay it (design D3). */
    if (input->resize_unreported) {
        input->resize_unreported = false;
        *event = (OvdEvent){ .kind = OVD_EVENT_KIND_RESIZE, .size = ovd_terminal_size(terminal) };
        return OVD_OK;
    }
    OvdEvent decoded;
    if (next_event(input, &decoded)) {
        *event = decoded;
        return OVD_OK;
    }

    /* Nothing complete. Headless time is virtual (R109, design D2): a
     * timeout that reaches the held bytes' deadline resolves them there
     * and returns at once; otherwise the whole timeout passes. */
    const uint64_t start = input->clock_ms;
    const uint64_t end = clock_after(start, timeout_ms);
    uint64_t deadline = 0;
    if (ovd_decoder_deadline(&input->decoder, &deadline) && (timeout_ms < 0 || end >= deadline)) {
        if (deadline > input->clock_ms) {
            input->clock_ms = deadline;
        }
        if (next_event(input, &decoded)) {
            *event = decoded;
            return OVD_OK;
        }
        /* The held bytes were discarded: the rest of the timeout passes,
         * and with a negative one nothing else can arrive. */
    }
    if (end > input->clock_ms) {
        input->clock_ms = end;
    }
    *event = (OvdEvent){ .kind = OVD_EVENT_KIND_NONE };
    return OVD_OK;
}

OVD_API OvdResult ovd_input_feed(OvdTerminal *terminal, const char *bytes, size_t length,
                                 size_t *accepted)
{
    if (terminal == nullptr || accepted == nullptr || (bytes == nullptr && length > 0)) {
        return OVD_ERROR_INVALID_ARGUMENT;
    }
    if (terminal->backend != OVD_BACKEND_HEADLESS) {
        return OVD_ERROR_UNSUPPORTED;
    }
    if (!terminal->input_on) {
        return OVD_ERROR_STATE;
    }
    OvdInput *input = &terminal->input;
    const size_t room = input->queue.size - input->length;
    const size_t take = length < room ? length : room;
    if (take > 0) {
        char *queue = input->queue.data;
        /* Compact when the new bytes would not fit after the live ones
         * (design D1); no allocation. */
        if (input->queue.size - (input->start + input->length) < take) {
            memmove(queue, queue + input->start, input->length);
            input->start = 0;
        }
        memcpy(queue + input->start + input->length, bytes, take);
        input->length += take;
    }
    *accepted = take;
    return OVD_OK;
}
