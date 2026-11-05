/* The input API (input.h): ovd_input_wait and ovd_input_feed over the
 * decoder (decode.h). One file of the terminal module, which owns the
 * input state (internal.h). The headless backend's bytes come from feed
 * and its time is a virtual clock; the system backend's come from stdin
 * through terminal_system.c, on the platform clock. research.md R102,
 * R103, R106, R107, R109 to R111, R114. */
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

static bool same_size(OvdSize a, OvdSize b)
{
    return a.cols == b.cols && a.rows == b.rows;
}

static void advance(OvdInput *input, uint64_t now_ms)
{
    if (now_ms > input->clock_ms) {
        input->clock_ms = now_ms;
    }
}

/* A resize wakeup (R111): re-read the size and store a resize only if it
 * differs from the last one reported. A failed read keeps the old size;
 * the next frame reports that error. */
static void check_size(OvdTerminal *terminal)
{
    OvdSize size;
    if (ovd_terminal_system_size(&size) == OVD_OK && !same_size(size, terminal->input.reported)) {
        terminal->input.reported = size;
        terminal->input.resize_unreported = true;
    }
}

/* The system backend's wait (R106, R107, R109, R111; OVD-18 design D3).
 * Time is the platform clock. Bytes are read only when the queue is empty,
 * into the whole queue, and decoded at the time they were read. A timeout
 * of 0 still polls once. A signal other than the resize wakeup ends a
 * platform wait early with nothing ready; the loop then waits out the
 * rest. */
static OvdResult wait_system(OvdTerminal *terminal, int32_t timeout_ms, OvdEvent *event)
{
    OvdInput *input = &terminal->input;
    const bool forever = timeout_ms < 0;
    advance(input, ovd_terminal_system_clock());
    const uint64_t end = clock_after(input->clock_ms, timeout_ms);
    bool polled = false;
    for (;;) {
        if (input->resize_unreported) {
            input->resize_unreported = false;
            *event = (OvdEvent){ .kind = OVD_EVENT_KIND_RESIZE, .size = input->reported };
            return OVD_OK;
        }
        if (next_event(input, event)) {
            return OVD_OK;
        }
        if (polled && !forever && input->clock_ms >= end) {
            *event = (OvdEvent){ .kind = OVD_EVENT_KIND_NONE };
            return OVD_OK;
        }
        /* The earlier of the timeout's end and the held bytes' deadline;
         * without either, no limit. */
        uint64_t limit = forever ? UINT64_MAX : end;
        uint64_t deadline = 0;
        if (ovd_decoder_deadline(&input->decoder, &deadline) && deadline < limit) {
            limit = deadline;
        }
        int32_t wait_ms = -1;
        if (limit != UINT64_MAX) {
            const uint64_t span = limit > input->clock_ms ? limit - input->clock_ms : 0;
            wait_ms = span > INT32_MAX ? INT32_MAX : (int32_t)span;
        }
        uint32_t ready = 0;
        OvdResult result = ovd_terminal_system_wait(&terminal->system, wait_ms, &ready);
        if (result != OVD_OK) {
            return result;
        }
        polled = true;
        advance(input, ovd_terminal_system_clock());
        if ((ready & OVD_PLATFORM_READY_RESIZE) != 0) {
            check_size(terminal);
        }
        if ((ready & OVD_PLATFORM_READY_INPUT) != 0) {
            /* next_event returned false, so the queue is empty. */
            size_t length = 0;
            bool resized = false;
            result = ovd_terminal_system_read(&terminal->system, input->queue.data,
                                              input->queue.size, &length, &resized);
            if (result != OVD_OK) {
                return result;
            }
            input->start = 0;
            input->length = length;
            if (resized) {
                check_size(terminal);
            }
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
    if (terminal->backend == OVD_BACKEND_SYSTEM) {
        return wait_system(terminal, timeout_ms, event);
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
