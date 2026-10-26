/*
 * Output buffer (private): the bytes of one frame, sent in one write
 * (FR-022).
 *
 * Owning module of OvdOutput. Its capacity is fixed by the block it
 * adopts, which the terminal allocates at each size to the worst case of
 * one frame (research.md R30). It never allocates and never grows: an
 * append that does not fit writes nothing and reports failure, and the
 * frame then fails (R30).
 *
 * The struct is declared here only so the terminal can embed it without
 * a separate allocation. Only output.c reads or changes its fields.
 * Invariant: length <= storage.size. A zero-initialized OvdOutput is a
 * valid empty output with capacity 0.
 */
#ifndef OVERDRIVE_SRC_OUTPUT_H
#define OVERDRIVE_SRC_OUTPUT_H

#include "alloc.h"

#include <stddef.h>

typedef struct OvdOutput {
    OvdBlock storage;
    size_t length;
} OvdOutput;

/* Releases the current storage (if any) through allocator, then takes
 * storage, which must come from the same allocator. The capacity becomes
 * storage.size and the output is empty. */
void ovd_output_adopt(OvdOutput *output, const OvdAllocator *allocator, OvdBlock storage);

/* Releases the storage; the output is then empty with capacity 0. */
void ovd_output_release(OvdOutput *output, const OvdAllocator *allocator);

/* Appends length bytes, all or nothing. Returns false, leaving the
 * output unchanged, if they do not fit in the remaining capacity, or if
 * bytes is nullptr while length is positive. length 0 always succeeds. */
[[nodiscard]] bool ovd_output_append(OvdOutput *output, const char *bytes, size_t length);

/* Empties the output for the next frame; the capacity is kept. */
void ovd_output_reset(OvdOutput *output);

size_t ovd_output_capacity(const OvdOutput *output);
size_t ovd_output_length(const OvdOutput *output);

/* The appended bytes, not NUL-terminated; nullptr while capacity is 0. */
const char *ovd_output_bytes(const OvdOutput *output);

#endif /* OVERDRIVE_SRC_OUTPUT_H */
