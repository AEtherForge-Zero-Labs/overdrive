/* SC-002 (T109): at least 10^7 seeded bytes of random and mutated input
 * through the decoder, in random-sized reads with time passing between
 * them. After every call it checks data-model.md's decoder invariants
 * (bounds, progress, no key events in paste state, valid events), and
 * after each recovery that a known sequence still decodes. Each read is
 * copied into an allocation of exactly its size, so under the sanitize
 * preset a read past it is reported. The decoder itself has no access to
 * an allocator.
 *
 * Usage: test_decode_fuzz [seed]. The seed is printed, so a failure can be
 * rerun. */
#include "decode.h"
#include "unicode.h"

#include <inttypes.h>
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

constexpr uint64_t DEFAULT_SEED = 0x0D1600020D160002ULL;
constexpr uint64_t BYTES_REQUIRED = 10000000;
constexpr size_t PIECE_MAX = 1024;
constexpr size_t READ_MAX = 48;
constexpr uint32_t DELAY_MS = 50;
constexpr size_t PROBE_EVERY = 32;

/* ---- PRNG: xorshift64* (Vigna) ---------------------------------------- */

static uint64_t prng_state;

static uint64_t next_random(void)
{
    prng_state ^= prng_state >> 12;
    prng_state ^= prng_state << 25;
    prng_state ^= prng_state >> 27;
    return prng_state * 0x2545F4914F6CDD1DULL;
}

static size_t below(size_t bound)
{
    return (size_t)(next_random() % bound);
}

/* ---- Inputs ----------------------------------------------------------- */

typedef struct Sequence {
    const char *bytes;
    size_t length;
} Sequence;

#define SEQ(literal) { (literal), sizeof(literal) - 1 }

/* Table sequences and their neighbours: keys, mouse, paste markers,
 * focus, replies to be discarded, and UTF-8. */
static const Sequence corpus[] = {
    SEQ("\x1b[A"), SEQ("\x1bOP"), SEQ("\x1b[1;5C"), SEQ("\x1b[15;2~"), SEQ("\x1b[24;16~"),
    SEQ("\x1b[Z"), SEQ("\x1b"), SEQ("\x1b["), SEQ("\x1bO"), SEQ("\x1b\x1b"), SEQ("\x1b" "a"),
    SEQ("\x1b[<0;1;1M"), SEQ("\x1b[<35;300;200M"), SEQ("\x1b[<65;2147483648;1M"),
    SEQ("\x1b[<2;7;9m"), SEQ("\x1b[200~"), SEQ("\x1b[201~"), SEQ("\x1b[20"), SEQ("\x1b[201"),
    SEQ("\x1b[I"), SEQ("\x1b[O"), SEQ("\x1b[?1;2c"), SEQ("\x1b[97;5u"), SEQ("\x1b[2 q"),
    SEQ("\r"), SEQ("\t"), SEQ("\x7f"), SEQ("\0"), SEQ("q"), SEQ("\xc3\xa9"),
    SEQ("\xf0\x9f\x98\x80"), SEQ("\xe2\x82"), SEQ("\xc3"), SEQ("\xed\xa0\x80"), SEQ("\xff"),
};
constexpr size_t CORPUS_SIZE = sizeof corpus / sizeof corpus[0];

static size_t put_random(char *out, size_t room, size_t count)
{
    const size_t length = count < room ? count : room;
    for (size_t i = 0; i < length; ++i) {
        out[i] = (char)next_random();
    }
    return length;
}

static size_t put_bytes(char *out, size_t room, const char *bytes, size_t length)
{
    const size_t size = length < room ? length : room;
    memcpy(out, bytes, size);
    return size;
}

/* One piece of input: random bytes, a table sequence, a mutation of one
 * (truncated, spliced, repeated, a byte flipped or inserted), an
 * over-long CSI, or a paste of random text. */
static size_t make_piece(char *out)
{
    size_t length = 0;
    const Sequence *a = &corpus[below(CORPUS_SIZE)];
    const Sequence *b = &corpus[below(CORPUS_SIZE)];
    switch (below(8)) {
    case 0:
    case 1:
        return put_random(out, PIECE_MAX, 1 + below(64));
    case 2:
        return put_bytes(out, PIECE_MAX, a->bytes, a->length);
    case 3: /* truncated */
        return put_bytes(out, PIECE_MAX, a->bytes, below(a->length + 1));
    case 4: /* spliced */
        length = put_bytes(out, PIECE_MAX, a->bytes, below(a->length + 1));
        {
            const size_t from = below(b->length + 1);
            length += put_bytes(out + length, PIECE_MAX - length, b->bytes + from,
                                b->length - from);
        }
        return length;
    case 5: /* repeated, then one byte flipped or inserted */
        for (size_t times = 1 + below(4); times > 0; --times) {
            length += put_bytes(out + length, PIECE_MAX - length, a->bytes, a->length);
        }
        if (length > 0 && below(2) == 0) {
            out[below(length)] ^= (char)(1u << below(8));
        } else if (length < PIECE_MAX) {
            const size_t at = below(length + 1);
            memmove(out + at + 1, out + at, length - at);
            out[at] = (char)next_random();
            ++length;
        }
        return length;
    case 6: /* past the parameter bound, or exactly at it */
        out[length++] = '\x1b';
        out[length++] = '[';
        for (size_t n = OVD_DECODER_PARAMS_MAX - 2 + below(300); n > 0; --n) {
            out[length++] = below(5) == 0 ? ';' : (char)('0' + below(10));
        }
        out[length++] = (char)('@' + below(63));
        return length;
    default: /* a paste of random text, sometimes ended */
        length = put_bytes(out, PIECE_MAX, "\x1b[200~", 6);
        length += put_random(out + length, PIECE_MAX - length, below(400));
        if (below(2) == 0) {
            length += put_bytes(out + length, PIECE_MAX - length, "\x1b[201~", 6);
        }
        return length;
    }
}

/* ---- Invariants (data-model.md, OvdDecoder) ------------------------- */

typedef struct Stats {
    uint64_t bytes;
    uint64_t calls;
    uint64_t kinds[OVD_EVENT_KIND_RESIZE + 1];
    uint64_t overflows;
    uint64_t held_markers;
    uint64_t probes;
} Stats;

static bool valid_utf8(const char *bytes, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        const OvdUtf8Unit unit = ovd_unicode_decode_utf8(bytes + offset, length - offset);
        if (unit.status != OVD_UTF8_SCALAR) {
            return false;
        }
        offset += unit.size;
    }
    return true;
}

static bool state_valid(const OvdDecoder *decoder)
{
    CHECK(decoder->state <= OVD_DECODER_PASTE);
    CHECK(decoder->params_length <= OVD_DECODER_PARAMS_MAX);
    CHECK(decoder->marker_length <= OVD_DECODER_MARKER_HELD_MAX);
    CHECK(decoder->marker_length == 0 || decoder->state == OVD_DECODER_PASTE);
    CHECK(decoder->utf8_length <= 3);
    CHECK(decoder->marker_length == 0 || decoder->utf8_length == 0);
    if (decoder->utf8_length > 0) {
        const OvdUtf8Unit unit =
            ovd_unicode_decode_utf8((const char *)decoder->utf8, decoder->utf8_length);
        CHECK(unit.status == OVD_UTF8_TRUNCATED);
        CHECK(decoder->state == OVD_DECODER_GROUND || decoder->state == OVD_DECODER_PASTE);
    }
    CHECK(!decoder->alt || (decoder->utf8_length > 0 && decoder->state == OVD_DECODER_GROUND));
    if (decoder->state != OVD_DECODER_CSI) {
        CHECK(decoder->params_length == 0 && !decoder->intermediates && !decoder->overflow);
    }
    uint64_t deadline = 0;
    const bool held = decoder->state == OVD_DECODER_ESCAPE || decoder->state == OVD_DECODER_CSI
        || decoder->state == OVD_DECODER_SS3
        || (decoder->state == OVD_DECODER_GROUND && decoder->utf8_length > 0);
    CHECK(ovd_decoder_deadline(decoder, &deadline) == held);
    return true;
}

/* Only the member that kind names is non-zero (data-model, OvdEvent). */
static bool only_named_member(const OvdEvent *event)
{
    OvdEvent expected;
    memset(&expected, 0, sizeof expected);
    expected.kind = event->kind;
    switch (event->kind) {
    case OVD_EVENT_KIND_KEY:
        expected.key.key = event->key.key;
        expected.key.codepoint = event->key.codepoint;
        expected.key.modifiers = event->key.modifiers;
        break;
    case OVD_EVENT_KIND_MOUSE:
        expected.mouse.action = event->mouse.action;
        expected.mouse.button = event->mouse.button;
        expected.mouse.x = event->mouse.x;
        expected.mouse.y = event->mouse.y;
        expected.mouse.modifiers = event->mouse.modifiers;
        break;
    case OVD_EVENT_KIND_PASTE_TEXT:
        expected.paste.bytes = event->paste.bytes;
        expected.paste.length = event->paste.length;
        break;
    default:
        break;
    }
    return memcmp(&expected, event, sizeof expected) == 0;
}

static bool event_valid(const OvdDecoder *decoder, OvdDecoderState before, const OvdEvent *event)
{
    CHECK(event->kind > OVD_EVENT_KIND_NONE && event->kind < OVD_EVENT_KIND_RESIZE);
    CHECK(only_named_member(event));
    if (before == OVD_DECODER_PASTE) {
        CHECK(event->kind == OVD_EVENT_KIND_PASTE_TEXT || event->kind == OVD_EVENT_KIND_PASTE_END);
    } else {
        CHECK(event->kind != OVD_EVENT_KIND_PASTE_TEXT && event->kind != OVD_EVENT_KIND_PASTE_END);
    }
    switch (event->kind) {
    case OVD_EVENT_KIND_KEY:
        CHECK(event->key.key <= OVD_KEY_F12);
        CHECK((event->key.modifiers & ~7u) == 0);
        if (event->key.key == OVD_KEY_CHAR) {
            CHECK(event->key.codepoint <= 0x10FFFF);
            CHECK(event->key.codepoint < 0xD800 || event->key.codepoint > 0xDFFF);
        } else {
            CHECK(event->key.codepoint == 0);
        }
        break;
    case OVD_EVENT_KIND_MOUSE:
        CHECK(event->mouse.action <= OVD_MOUSE_ACTION_WHEEL);
        CHECK(event->mouse.button <= OVD_MOUSE_BUTTON_WHEEL_DOWN);
        CHECK((event->mouse.action == OVD_MOUSE_ACTION_WHEEL)
              == (event->mouse.button >= OVD_MOUSE_BUTTON_WHEEL_UP));
        CHECK(event->mouse.x >= 0 && event->mouse.y >= 0);
        CHECK((event->mouse.modifiers & ~7u) == 0);
        break;
    case OVD_EVENT_KIND_PASTE_TEXT:
        CHECK(event->paste.bytes == decoder->text);
        CHECK(event->paste.length > 0 && event->paste.length <= decoder->text_capacity);
        CHECK(valid_utf8(event->paste.bytes, event->paste.length));
        break;
    default:
        break;
    }
    return true;
}

/* One read: copied into an allocation of exactly its size, then decoded
 * until every byte is consumed. Each call is checked. */
static bool read_bytes(OvdDecoder *decoder, const char *bytes, size_t length, uint64_t now_ms,
                       Stats *stats, OvdEvent *last)
{
    char *copy = length > 0 ? malloc(length) : nullptr;
    CHECK(length == 0 || copy != nullptr);
    if (length > 0) {
        memcpy(copy, bytes, length);
    }
    size_t offset = 0;
    bool ok = true;
    for (size_t calls = 0; ok; ++calls) {
        if (calls > 4 * length + 8) {
            fprintf(stderr, "%s:%d: no progress\n", __FILE__, __LINE__);
            ok = false;
            break;
        }
        const OvdDecoderState before = decoder->state;
        size_t consumed = SIZE_MAX;
        OvdEvent event;
        const bool produced = ovd_decoder_next(decoder, copy == nullptr ? nullptr : copy + offset,
                                               length - offset, now_ms, &consumed, &event);
        ++stats->calls;
        ok = consumed <= length - offset && (produced || consumed > 0 || offset == length)
            && state_valid(decoder) && (!produced || event_valid(decoder, before, &event));
        if (!ok) {
            fprintf(stderr, "%s:%d: call %" PRIu64 " failed (before state %d)\n", __FILE__,
                    __LINE__, stats->calls, (int)before);
            break;
        }
        offset += consumed;
        if (decoder->overflow) {
            ++stats->overflows;
        }
        if (decoder->marker_length > 0) {
            ++stats->held_markers;
        }
        if (produced) {
            ++stats->kinds[event.kind];
            if (last != nullptr) {
                *last = event;
            }
        } else if (offset == length) {
            break;
        }
    }
    free(copy);
    stats->bytes += length;
    return ok;
}

/* Recovery: let the delay pass, end any paste, then a known sequence must
 * decode to exactly its event (SC-002: "never stops returning later
 * valid events"). */
static bool probe(OvdDecoder *decoder, uint64_t *now_ms, Stats *stats)
{
    *now_ms += 1000;
    CHECK(read_bytes(decoder, nullptr, 0, *now_ms, stats, nullptr));
    if (decoder->state == OVD_DECODER_PASTE) {
        OvdEvent last = { 0 };
        CHECK(read_bytes(decoder, "\x1b[201~", 6, *now_ms, stats, &last));
        CHECK(last.kind == OVD_EVENT_KIND_PASTE_END);
    }
    CHECK(decoder->state == OVD_DECODER_GROUND && decoder->utf8_length == 0);
    size_t consumed = 0;
    OvdEvent event;
    CHECK(ovd_decoder_next(decoder, "\x1b[1;5C", 6, *now_ms, &consumed, &event));
    CHECK(consumed == 6);
    CHECK(event.kind == OVD_EVENT_KIND_KEY && event.key.key == OVD_KEY_RIGHT
          && event.key.modifiers == OVD_MODIFIER_CTRL);
    CHECK(!ovd_decoder_next(decoder, nullptr, 0, *now_ms, &consumed, &event));
    ++stats->probes;
    return true;
}

static bool fuzz(uint64_t seed, size_t text_capacity)
{
    prng_state = seed;
    char text[256];
    OvdDecoder decoder;
    CHECK(text_capacity <= sizeof text);
    CHECK(ovd_decoder_init(&decoder, DELAY_MS, text, text_capacity));
    Stats stats = { 0 };
    uint64_t now_ms = 0;
    char piece[PIECE_MAX];
    for (size_t pieces = 1; stats.bytes < BYTES_REQUIRED / 2; ++pieces) {
        const size_t length = make_piece(piece);
        size_t offset = 0;
        while (offset < length) {
            const size_t remaining = length - offset;
            const size_t size = 1 + below(remaining < READ_MAX ? remaining : READ_MAX);
            now_ms += below(4) == 0 ? below(2 * DELAY_MS) : 0;
            if (!read_bytes(&decoder, piece + offset, size, now_ms, &stats, nullptr)) {
                fprintf(stderr, "seed 0x%016" PRIx64 ", piece %zu, offset %zu\n", seed, pieces,
                        offset);
                return false;
            }
            offset += size;
        }
        if (pieces % PROBE_EVERY == 0 && !probe(&decoder, &now_ms, &stats)) {
            fprintf(stderr, "seed 0x%016" PRIx64 ", probe after piece %zu\n", seed, pieces);
            return false;
        }
    }
    printf("capacity %3zu: %" PRIu64 " bytes, %" PRIu64 " calls, %" PRIu64 " probes;"
           " key %" PRIu64 ", mouse %" PRIu64 ", paste %" PRIu64 "/%" PRIu64 "/%" PRIu64
           ", focus %" PRIu64 "/%" PRIu64 "; overflow %" PRIu64 ", held marker %" PRIu64 "\n",
           text_capacity, stats.bytes, stats.calls, stats.probes,
           stats.kinds[OVD_EVENT_KIND_KEY], stats.kinds[OVD_EVENT_KIND_MOUSE],
           stats.kinds[OVD_EVENT_KIND_PASTE_START], stats.kinds[OVD_EVENT_KIND_PASTE_TEXT],
           stats.kinds[OVD_EVENT_KIND_PASTE_END], stats.kinds[OVD_EVENT_KIND_FOCUS_IN],
           stats.kinds[OVD_EVENT_KIND_FOCUS_OUT], stats.overflows, stats.held_markers);
    /* The run reached every path it is meant to exercise. */
    for (int kind = OVD_EVENT_KIND_KEY; kind <= OVD_EVENT_KIND_FOCUS_OUT; ++kind) {
        CHECK(stats.kinds[kind] > 0);
    }
    CHECK(stats.overflows > 0 && stats.held_markers > 0 && stats.probes > 0);
    return true;
}

int main(int argc, char **argv)
{
    uint64_t seed = DEFAULT_SEED;
    if (argc > 1) {
        seed = strtoull(argv[1], nullptr, 0);
    }
    printf("seed 0x%016" PRIx64 "\n", seed);
    /* Half the bytes with the smallest chunk buffer, half with a roomy one:
     * chunking changes, invariants must not. */
    if (!fuzz(seed, OVD_DECODER_TEXT_MIN) || !fuzz(seed ^ 0xA5A5A5A5A5A5A5A5ULL, 256)) {
        return EXIT_FAILURE;
    }
    puts("decode fuzz: 10^7 bytes, every invariant held after every call");
    return EXIT_SUCCESS;
}
