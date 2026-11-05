/* Input decoder: terminal bytes to events (FR-011 to FR-016, FR-019).
 * Internal to Overdrive. Rules: research R103 (paste), R104 (layers),
 * R105 (key encodings), R109 (escape delay); state and invariants:
 * data-model.md, OvdDecoder; interface: openspec change i53-decoder,
 * design D2 to D5. */
#ifndef OVERDRIVE_SRC_DECODE_H
#define OVERDRIVE_SRC_DECODE_H

#include <overdrive/input.h>

#include <stddef.h>
#include <stdint.h>

/* The bounded CSI parameter area (R104). A longer sequence is consumed to
 * its final byte and discarded. */
constexpr uint8_t OVD_DECODER_PARAMS_MAX = 64;

/* The smallest paste chunk buffer: the most text one input byte can add
 * (a 5-byte end-marker prefix given back, then a U+FFFD). */
constexpr size_t OVD_DECODER_TEXT_MIN = 8;

/* The bytes of ESC [ 2 0 1 ~ less its last, held back in a paste (R103). */
constexpr uint8_t OVD_DECODER_MARKER_HELD_MAX = 5;

typedef enum OvdDecoderState : uint8_t {
    OVD_DECODER_GROUND = 0,
    OVD_DECODER_ESCAPE, /* after ESC */
    OVD_DECODER_CSI,    /* after ESC [ */
    OVD_DECODER_SS3,    /* after ESC O */
    OVD_DECODER_PASTE,  /* between CSI 200 ~ and CSI 201 ~ */
} OvdDecoderState;

/* Partial-sequence state carried between reads (R104). Embedded in the
 * terminal; only decode.c writes the fields.
 *
 * Invariants, kept by every call:
 * - params_length <= OVD_DECODER_PARAMS_MAX; past it overflow is set and
 *   parameter bytes are consumed, not stored;
 * - marker_length <= OVD_DECODER_MARKER_HELD_MAX, and is 0 outside paste;
 * - utf8_length <= 3, bytes utf8[0..utf8_length) are a proper prefix of
 *   a well-formed scalar, and marker_length and utf8_length are never
 *   both non-zero;
 * - alt is set only while a partial scalar after ESC is held;
 * - in paste state no key, mouse, or focus event is produced. */
typedef struct OvdDecoder {
    char *text;               /* paste chunk buffer, owned by the caller */
    size_t text_capacity;
    uint64_t last_ms;         /* when the last byte was consumed */
    uint32_t delay_ms;        /* escape delay, R109 */
    OvdDecoderState state;
    uint8_t params_length;
    bool intermediates;       /* an intermediate byte was seen in this CSI */
    bool overflow;            /* this CSI is discarded at its final byte */
    uint8_t marker_length;    /* bytes of ESC [ 2 0 1 ~ matched in paste */
    uint8_t utf8_length;      /* bytes of a partial scalar held */
    bool alt;                 /* the held scalar followed an ESC */
    char params[OVD_DECODER_PARAMS_MAX];
    unsigned char utf8[3];
} OvdDecoder;

/* Sets *decoder to its initial ground state with the given escape delay
 * and paste chunk buffer text[0..text_capacity), which must outlive it.
 * Returns false, leaving *decoder untouched, when decoder or text is
 * nullptr or text_capacity < OVD_DECODER_TEXT_MIN. */
[[nodiscard]] bool ovd_decoder_init(OvdDecoder *decoder, uint32_t escape_delay_ms, char *text,
                                    size_t text_capacity);

/* Decodes from bytes[0..length), which arrived at now_ms on the caller's
 * monotonic clock, until one event is produced or every byte is
 * consumed. Stores the count of bytes consumed in *consumed, and returns
 * true when *event holds an event (only the member its kind names is
 * non-zero). Returns false, with *event zeroed, when the bytes were
 * consumed without completing one.
 *
 * Held bytes whose last byte is at least the escape delay before now_ms
 * resolve first, before any new byte (design D3). An event can leave the
 * byte that ended it unconsumed when that byte starts something new.
 * Each call consumes at least one byte or returns an event, so a loop
 * calling it again with the bytes after *consumed makes progress. A
 * PASTE_TEXT event's bytes point into the chunk buffer and are valid
 * until the next call. Reads only bytes[0..length); never allocates.
 *
 * Returns false and consumes nothing when decoder, consumed, or event is
 * nullptr, or bytes is nullptr with length > 0. */
[[nodiscard]] bool ovd_decoder_next(OvdDecoder *decoder, const char *bytes, size_t length,
                                    uint64_t now_ms, size_t *consumed, OvdEvent *event);

/* When bytes are held outside paste, stores the time at which they
 * resolve (the last byte's time plus the escape delay, saturating) in
 * *deadline_ms and returns true. Returns false otherwise, or for a
 * nullptr argument. */
[[nodiscard]] bool ovd_decoder_deadline(const OvdDecoder *decoder, uint64_t *deadline_ms);

#endif /* OVERDRIVE_SRC_DECODE_H */
