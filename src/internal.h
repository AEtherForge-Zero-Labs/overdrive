/*
 * Terminal state (private). FR-002, FR-012, FR-014, FR-022; research.md
 * R21 to R25, R31, R35, R44.
 *
 * Owning module of struct OvdTerminal (terminal.c). The struct is
 * declared here for the terminal module's own files; only they read or
 * change its fields. It holds only what the headless backend needs; the
 * system backend's fields arrive with it (OVD-11, R44).
 */
#ifndef OVERDRIVE_SRC_INTERNAL_H
#define OVERDRIVE_SRC_INTERNAL_H

#include "alloc.h"
#include "buffer.h"
#include "output.h"

#include <overdrive/render.h>
#include <overdrive/terminal.h>

#include <stddef.h>

/* Invariants:
 * - self is the block this struct lives in, from allocator;
 * - grids[0] and grids[1] are both of size, and output's capacity is
 *   ovd_diff_bound for size and synchronized;
 * - grids[screen] is what the terminal shows when screen_known; the
 *   other grid is the frame, and canvas is open on it exactly while
 *   frame_open;
 * - sink_running implies !frame_open (end closes the frame before the
 *   sink runs, R24);
 * - has_pending implies pending has positive components. */
struct OvdTerminal {
    OvdAllocator allocator;
    OvdBlock self;

    OvdBackend backend;
    OvdColorDepth depth;
    bool synchronized;
    OvdSink sink;
    void *sink_context;

    OvdSize size;
    OvdSize pending;
    bool has_pending;

    OvdBuffer grids[2];
    size_t screen;
    bool screen_known;
    OvdOutput output;
    OvdCanvas canvas;

    bool frame_open;
    bool sink_running;
};

#endif /* OVERDRIVE_SRC_INTERNAL_H */
