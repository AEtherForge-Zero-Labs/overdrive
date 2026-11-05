/* Headless input (T110 to T112): ovd_input_wait and ovd_input_feed over
 * the decoder. Public API only, with the counting allocator. research.md
 * R102 (wait), R103 (paste), R109 (virtual time), R110 (feed, resize),
 * R114 (allocation); openspec change i54-headless-input, design D1-D5.
 *
 * The signal test reads dispositions with sigaction, which is POSIX.1,
 * not ISO C: under -std=c23 glibc declares it only when a feature macro
 * asks for it, as in src/platform/platform_posix.c. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "support/test_alloc.h"

#include <overdrive/overdrive.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <signal.h>
#endif

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

/* R110's fixed input queue. */
constexpr size_t QUEUE_SIZE = 4096;

/* ---- Fixture --------------------------------------------------------- */

typedef struct Fixture {
    TestAlloc alloc;
    OvdAllocator allocator;
    OvdTerminalConfig config;
    OvdTerminal *terminal;
} Fixture;

static void fixture_config(Fixture *f, bool input)
{
    test_alloc_init(&f->alloc);
    f->allocator = test_alloc_allocator(&f->alloc);
    f->config = ovd_terminal_config_default();
    f->config.backend = OVD_BACKEND_HEADLESS;
    f->config.headless_size = (OvdSize){ 80, 24 };
    f->config.allocator = &f->allocator;
    f->config.input = input;
    f->terminal = nullptr;
}

static bool fixture_open(Fixture *f, bool input)
{
    fixture_config(f, input);
    CHECK(ovd_terminal_create(&f->config, &f->terminal) == OVD_OK);
    return true;
}

static bool fixture_done(Fixture *f)
{
    ovd_terminal_destroy(f->terminal);
    f->terminal = nullptr;
    const bool clean = f->alloc.live_blocks == 0 && f->alloc.violations == 0;
    test_alloc_release_all(&f->alloc);
    CHECK(clean);
    return true;
}

static bool feed_all(OvdTerminal *t, const char *bytes, size_t length)
{
    size_t accepted = SIZE_MAX;
    CHECK(ovd_input_feed(t, bytes, length, &accepted) == OVD_OK);
    CHECK(accepted == length);
    return true;
}

#define FEED(t, literal) CHECK(feed_all((t), (literal), sizeof(literal) - 1))

static bool wait_kind(OvdTerminal *t, int32_t timeout, OvdEventKind kind, OvdEvent *out)
{
    OvdEvent event;
    memset(&event, 0xA5, sizeof event);
    CHECK(ovd_input_wait(t, timeout, &event) == OVD_OK);
    if (event.kind != kind) {
        fprintf(stderr, "  wait(%d): kind %d, expected %d\n", (int)timeout, (int)event.kind,
                (int)kind);
        return false;
    }
    if (out != nullptr) {
        *out = event;
    }
    return true;
}

static bool wait_key(OvdTerminal *t, int32_t timeout, OvdKey key, uint32_t codepoint,
                     uint8_t modifiers)
{
    OvdEvent event;
    CHECK(wait_kind(t, timeout, OVD_EVENT_KIND_KEY, &event));
    if (event.key.key != key || event.key.codepoint != codepoint
        || event.key.modifiers != modifiers) {
        fprintf(stderr, "  key %d U+%04X mods %d, expected %d U+%04X mods %d\n",
                (int)event.key.key, (unsigned)event.key.codepoint, (int)event.key.modifiers,
                (int)key, (unsigned)codepoint, (int)modifiers);
        return false;
    }
    return true;
}

/* ---- State and arguments (T110) ------------------------------------- */

static bool test_input_off(void)
{
    Fixture f;
    CHECK(fixture_open(&f, false));
    OvdEvent event = { .kind = OVD_EVENT_KIND_KEY };
    size_t accepted = 7;
    CHECK(ovd_input_wait(f.terminal, 0, &event) == OVD_ERROR_STATE);
    CHECK(event.kind == OVD_EVENT_KIND_KEY); /* untouched on error */
    CHECK(ovd_input_feed(f.terminal, "a", 1, &accepted) == OVD_ERROR_STATE);
    CHECK(accepted == 7);
    /* A resize with input off is not queued as an event for later. */
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 10, 10 }) == OVD_OK);
    CHECK(ovd_input_wait(f.terminal, 0, &event) == OVD_ERROR_STATE);
    return fixture_done(&f);
}

static bool test_arguments(void)
{
    Fixture f;
    CHECK(fixture_open(&f, true));
    OvdEvent event;
    size_t accepted = 7;
    CHECK(ovd_input_wait(nullptr, 0, &event) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_input_wait(f.terminal, 0, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_input_feed(nullptr, "a", 1, &accepted) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_input_feed(f.terminal, "a", 1, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_input_feed(f.terminal, nullptr, 1, &accepted) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(accepted == 7);
    CHECK(ovd_input_feed(f.terminal, nullptr, 0, &accepted) == OVD_OK && accepted == 0);
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_NONE, nullptr));
    return fixture_done(&f);
}

/* R110: feed takes up to the queue's free space and reports it. The
 * queue is compacted as it drains (design D1); order is kept. */
static bool test_feed_limit(void)
{
    Fixture f;
    CHECK(fixture_open(&f, true));
    static char bytes[2 * QUEUE_SIZE];
    for (size_t i = 0; i < sizeof bytes; ++i) {
        bytes[i] = (char)('a' + i % 26);
    }
    size_t accepted = 0;
    CHECK(ovd_input_feed(f.terminal, bytes, sizeof bytes, &accepted) == OVD_OK);
    CHECK(accepted == QUEUE_SIZE);
    CHECK(ovd_input_feed(f.terminal, bytes, 1, &accepted) == OVD_OK && accepted == 0);

    /* Drain 100, then the free space is exactly 100. */
    size_t next = 0;
    for (; next < 100; ++next) {
        CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, (uint32_t)('a' + next % 26), 0));
    }
    CHECK(ovd_input_feed(f.terminal, bytes + QUEUE_SIZE, sizeof bytes - QUEUE_SIZE, &accepted)
          == OVD_OK);
    CHECK(accepted == 100);
    /* Everything arrives in order: the first QUEUE_SIZE + 100 bytes. */
    for (; next < QUEUE_SIZE + 100; ++next) {
        CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, (uint32_t)('a' + next % 26), 0));
    }
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(ovd_input_feed(f.terminal, bytes, QUEUE_SIZE, &accepted) == OVD_OK);
    CHECK(accepted == QUEUE_SIZE);
    return fixture_done(&f);
}

/* R102, R109: a poll and a timed wait with nothing queued return NONE; a
 * negative wait with nothing queued returns NONE at once. */
static bool test_empty_waits(void)
{
    Fixture f;
    CHECK(fixture_open(&f, true));
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(wait_kind(f.terminal, 100, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(wait_kind(f.terminal, OVD_WAIT_FOREVER, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(wait_kind(f.terminal, INT32_MIN, OVD_EVENT_KIND_NONE, nullptr));
    FEED(f.terminal, "q");
    CHECK(wait_key(f.terminal, OVD_WAIT_FOREVER, OVD_KEY_CHAR, 'q', 0));
    CHECK(wait_kind(f.terminal, OVD_WAIT_FOREVER, OVD_EVENT_KIND_NONE, nullptr));
    return fixture_done(&f);
}

/* R109, design D2: headless time is virtual and cumulative; feed does not
 * advance it. */
static bool test_virtual_time(void)
{
    Fixture f;
    CHECK(fixture_open(&f, true)); /* escape delay 50 */
    FEED(f.terminal, "\x1b");
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(wait_kind(f.terminal, 49, OVD_EVENT_KIND_NONE, nullptr));
    CHECK(wait_key(f.terminal, 1, OVD_KEY_ESCAPE, 0, 0)); /* 49 + 1 */

    FEED(f.terminal, "\x1b");
    CHECK(wait_key(f.terminal, 50, OVD_KEY_ESCAPE, 0, 0));
    FEED(f.terminal, "\x1b");
    CHECK(wait_key(f.terminal, 1000, OVD_KEY_ESCAPE, 0, 0));
    FEED(f.terminal, "\x1b");
    CHECK(wait_key(f.terminal, OVD_WAIT_FOREVER, OVD_KEY_ESCAPE, 0, 0));

    /* A key fed within the delay is Alt + key; after it, Escape and key. */
    FEED(f.terminal, "\x1b");
    CHECK(wait_kind(f.terminal, 10, OVD_EVENT_KIND_NONE, nullptr));
    FEED(f.terminal, "a");
    CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, 'a', OVD_MODIFIER_ALT));
    FEED(f.terminal, "\x1b");
    CHECK(wait_key(f.terminal, 60, OVD_KEY_ESCAPE, 0, 0));
    FEED(f.terminal, "a");
    CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, 'a', 0));

    /* A whole sequence fed at once is one event, at any timeout. */
    FEED(f.terminal, "\x1b[1;5C");
    CHECK(wait_key(f.terminal, 0, OVD_KEY_RIGHT, 0, OVD_MODIFIER_CTRL));
    /* An unfinished sequence is discarded when the delay passes. */
    FEED(f.terminal, "\x1b[1;5");
    CHECK(wait_kind(f.terminal, 100, OVD_EVENT_KIND_NONE, nullptr));
    FEED(f.terminal, "C");
    CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, 'C', 0));
    CHECK(fixture_done(&f));

    /* Delay 0: a lone ESC resolves at the first wait. */
    fixture_config(&f, true);
    f.config.escape_delay_ms = 0;
    CHECK(ovd_terminal_create(&f.config, &f.terminal) == OVD_OK);
    FEED(f.terminal, "\x1b");
    CHECK(wait_key(f.terminal, 0, OVD_KEY_ESCAPE, 0, 0));
    return fixture_done(&f);
}

/* R102: inside the sink, wait is OVD_ERROR_STATE; with a frame open it
 * works. Feed is allowed in the sink (design D5). */
typedef struct SinkProbe {
    OvdTerminal *terminal;
    OvdResult wait;
    OvdResult feed;
    size_t calls;
} SinkProbe;

static bool probe_sink(void *context, const char *bytes, size_t length)
{
    (void)bytes;
    (void)length;
    SinkProbe *p = context;
    OvdEvent event;
    size_t accepted = 0;
    p->wait = ovd_input_wait(p->terminal, 0, &event);
    p->feed = ovd_input_feed(p->terminal, "z", 1, &accepted);
    ++p->calls;
    return true;
}

static bool test_wait_and_frames(void)
{
    Fixture f;
    SinkProbe probe = { .wait = OVD_OK, .feed = OVD_ERROR_STATE };
    fixture_config(&f, true);
    f.config.sink = probe_sink;
    f.config.sink_context = &probe;
    CHECK(ovd_terminal_create(&f.config, &f.terminal) == OVD_OK);
    probe.terminal = f.terminal;

    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    FEED(f.terminal, "k");
    CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, 'k', 0)); /* frame open */
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    CHECK(probe.calls == 1);
    CHECK(probe.wait == OVD_ERROR_STATE);
    CHECK(probe.feed == OVD_OK);
    CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, 'z', 0)); /* fed from the sink */
    return fixture_done(&f);
}

/* R110, design D3: a stored size is reported once, before queued bytes,
 * with the latest size. */
static bool test_resize(void)
{
    Fixture f;
    CHECK(fixture_open(&f, true));
    OvdEvent event;
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 100, 30 }) == OVD_OK);
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_RESIZE, &event));
    CHECK(event.size.cols == 100 && event.size.rows == 30);
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_NONE, nullptr));

    FEED(f.terminal, "a");
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 1, 1 }) == OVD_OK);
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 40, 12 }) == OVD_OK);
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_RESIZE, &event));
    CHECK(event.size.cols == 40 && event.size.rows == 12);
    CHECK(wait_key(f.terminal, 0, OVD_KEY_CHAR, 'a', 0));
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_NONE, nullptr));

    /* A frame that adopts the size does not swallow the report. */
    CHECK(ovd_terminal_set_size(f.terminal, (OvdSize){ 20, 5 }) == OVD_OK);
    OvdCanvas *canvas = nullptr;
    CHECK(ovd_frame_begin(f.terminal, &canvas) == OVD_OK);
    CHECK(ovd_frame_end(f.terminal) == OVD_OK);
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_RESIZE, &event));
    CHECK(event.size.cols == 20 && event.size.rows == 5);
    return fixture_done(&f);
}

/* FR-035: a headless terminal changes no signal disposition. */
static bool test_signal_dispositions(void)
{
#if defined(_WIN32)
    return true; /* POSIX only (T110) */
#else
    enum { SIGNALS = 32 };
    struct sigaction before[SIGNALS];
    bool known[SIGNALS];
    for (int s = 1; s < SIGNALS; ++s) {
        known[s] = sigaction(s, nullptr, &before[s]) == 0;
    }
    Fixture f;
    fixture_config(&f, true);
    f.config.restore_on_exit = true;
    f.config.mouse = true;
    f.config.bracketed_paste = true;
    f.config.focus_events = true;
    CHECK(ovd_terminal_create(&f.config, &f.terminal) == OVD_OK);
    for (int s = 1; s < SIGNALS; ++s) {
        struct sigaction during;
        if (known[s]) {
            CHECK(sigaction(s, nullptr, &during) == 0);
            CHECK(during.sa_handler == before[s].sa_handler);
            CHECK(during.sa_flags == before[s].sa_flags);
        }
    }
    CHECK(fixture_done(&f));
    for (int s = 1; s < SIGNALS; ++s) {
        struct sigaction after;
        if (known[s]) {
            CHECK(sigaction(s, nullptr, &after) == 0);
            CHECK(after.sa_handler == before[s].sa_handler);
            CHECK(after.sa_flags == before[s].sa_flags);
        }
    }
    return true;
#endif
}

/* ---- Paste lifetime (T111) ------------------------------------------ */

/* A paste longer than three queues, fed in pieces between waits, arrives
 * whole and in order through START, TEXT, END; each chunk is checked when
 * it is returned, the time it is valid until (R103). */
static bool test_long_paste(void)
{
    Fixture f;
    CHECK(fixture_open(&f, true));
    const size_t text_length = 3 * QUEUE_SIZE + 1234;
    static char input[3 * 4096 + 1234 + 12];
    static char expected[3 * 4096 + 1234];
    size_t length = 0;
    memcpy(input, "\x1b[200~", 6);
    length = 6;
    /* Printable text, newlines, lone ESCs (a 1-byte give-back), and near
     * misses of the end marker, ESC [ 2 0 1 x (a 5-byte give-back). */
    static const char near_miss[] = "\x1b[201x";
    size_t near_misses = 0;
    for (size_t i = 0; i < text_length;) {
        if (i % 251 == 250 && i + sizeof near_miss - 1 <= text_length) {
            memcpy(expected + i, near_miss, sizeof near_miss - 1);
            i += sizeof near_miss - 1;
            ++near_misses;
            continue;
        }
        expected[i] = (i % 97 == 96) ? '\x1b' : (i % 97 == 95) ? '\n' : (char)('A' + i % 57);
        ++i;
    }
    CHECK(near_misses > 10);
    memcpy(input + length, expected, text_length);
    length += text_length;
    memcpy(input + length, "\x1b[201~", 6);
    length += 6;

    static char received[3 * 4096 + 1234];
    size_t received_length = 0;
    size_t starts = 0;
    size_t ends = 0;
    size_t chunks = 0;
    size_t offset = 0;
    size_t piece = 1;
    while (ends == 0) {
        if (offset < length) {
            const size_t want = piece < length - offset ? piece : length - offset;
            size_t accepted = 0;
            CHECK(ovd_input_feed(f.terminal, input + offset, want, &accepted) == OVD_OK);
            offset += accepted;
            piece = piece * 3 + 1; /* 1, 4, 13, ... up to a full queue */
            if (piece > 2 * QUEUE_SIZE) {
                piece = 7;
            }
        }
        for (;;) {
            OvdEvent event;
            CHECK(ovd_input_wait(f.terminal, 0, &event) == OVD_OK);
            if (event.kind == OVD_EVENT_KIND_NONE) {
                break;
            }
            if (event.kind == OVD_EVENT_KIND_PASTE_START) {
                CHECK(starts == 0 && chunks == 0);
                ++starts;
            } else if (event.kind == OVD_EVENT_KIND_PASTE_TEXT) {
                CHECK(starts == 1 && ends == 0);
                CHECK(event.paste.length > 0);
                CHECK(received_length + event.paste.length <= text_length);
                /* Compared now: valid until the next input call. */
                CHECK(memcmp(event.paste.bytes, expected + received_length,
                             event.paste.length)
                      == 0);
                memcpy(received + received_length, event.paste.bytes, event.paste.length);
                received_length += event.paste.length;
                ++chunks;
            } else {
                CHECK(event.kind == OVD_EVENT_KIND_PASTE_END);
                ++ends;
            }
        }
        /* Everything fed and drained: the paste must have ended. */
        CHECK(offset < length || ends == 1);
    }
    CHECK(offset == length && starts == 1 && ends == 1);
    CHECK(received_length == text_length);
    CHECK(memcmp(received, expected, text_length) == 0);
    CHECK(chunks > 3);
    CHECK(wait_kind(f.terminal, 0, OVD_EVENT_KIND_NONE, nullptr));
    return fixture_done(&f);
}

/* ---- Allocation at create (T112, 001 R21) --------------------------- */

/* Refusing each of create's allocations with input on leaves nothing
 * allocated and *out untouched; input on adds exactly two blocks. */
static bool test_create_allocation_failure(void)
{
    Fixture f;
    CHECK(fixture_open(&f, false));
    const size_t without_input = f.alloc.allocate_calls;
    CHECK(fixture_done(&f));
    CHECK(fixture_open(&f, true));
    const size_t with_input = f.alloc.allocate_calls;
    CHECK(fixture_done(&f));
    CHECK(with_input == without_input + 2);

    for (size_t refuse = 1; refuse <= with_input; ++refuse) {
        fixture_config(&f, true);
        f.alloc.refuse_call = refuse;
        OvdTerminal *sentinel = (OvdTerminal *)&f;
        OvdTerminal *out = sentinel;
        CHECK(ovd_terminal_create(&f.config, &out) == OVD_ERROR_ALLOCATION);
        CHECK(out == sentinel);
        CHECK(f.alloc.live_blocks == 0 && f.alloc.violations == 0);
        test_alloc_release_all(&f.alloc);
    }
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "input_off", test_input_off },
        { "arguments", test_arguments },
        { "feed_limit", test_feed_limit },
        { "empty_waits", test_empty_waits },
        { "virtual_time", test_virtual_time },
        { "wait_and_frames", test_wait_and_frames },
        { "resize", test_resize },
        { "signal_dispositions", test_signal_dispositions },
        { "long_paste", test_long_paste },
        { "create_allocation_failure", test_create_allocation_failure },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (tests[i].run()) {
            printf("ok   %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
        fflush(stdout);
    }
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    puts("input: state, arguments, feed limit, virtual time, sink, resize, signals, paste, "
         "allocation passed");
    return EXIT_SUCCESS;
}
