#include "detect.h"
#include "platform/platform.h"

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

/* ---- Fake environment ------------------------------------------------ */

/* An environment written as "NAME=value" entries (Art. VI), reached only
 * through the lookup's context. The lookup records every name asked. */
typedef struct FakeEnvironment {
    const char *entries[4];
    const char *asked[8];
    size_t asked_count;
} FakeEnvironment;

static const char *fake_lookup(void *context, const char *name)
{
    FakeEnvironment *environment = context;
    if (environment->asked_count < sizeof environment->asked / sizeof environment->asked[0]) {
        environment->asked[environment->asked_count] = name;
    }
    ++environment->asked_count;
    for (size_t i = 0; i < sizeof environment->entries / sizeof environment->entries[0]; ++i) {
        const char *entry = environment->entries[i];
        const size_t length = strlen(name);
        if (entry != nullptr && strncmp(entry, name, length) == 0 && entry[length] == '=') {
            return entry + length + 1;
        }
    }
    return nullptr;
}

static const char *depth_name(OvdColorDepth depth)
{
    switch (depth) {
    case OVD_COLOR_DEPTH_NONE:
        return "NONE";
    case OVD_COLOR_DEPTH_16:
        return "16";
    case OVD_COLOR_DEPTH_256:
        return "256";
    case OVD_COLOR_DEPTH_TRUECOLOR:
        return "TRUECOLOR";
    }
    return "out of range";
}

/* ---- R10 / R36 rules ------------------------------------------------- */

typedef struct DetectCase {
    const char *entries[4];
    bool virtual_terminal;
    OvdColorDepth expected;
    const char *why;
} DetectCase;

static const DetectCase cases[] = {
    /* Nothing matches. */
    { {}, false, OVD_COLOR_DEPTH_16, "empty environment" },
    { { "TERM=xterm" }, false, OVD_COLOR_DEPTH_16, "plain TERM" },
    { { "TERM=" }, false, OVD_COLOR_DEPTH_16, "empty TERM" },

    /* 1. NO_COLOR, when set and non-empty, beats everything. */
    { { "NO_COLOR=1" }, false, OVD_COLOR_DEPTH_NONE, "NO_COLOR" },
    { { "NO_COLOR=0" }, false, OVD_COLOR_DEPTH_NONE, "any non-empty value counts" },
    { { "NO_COLOR=1", "COLORTERM=truecolor", "TERM=xterm-256color" }, true,
      OVD_COLOR_DEPTH_NONE, "NO_COLOR beats rules 2-4" },
    { { "COLORTERM=24bit", "NO_COLOR=x" }, false, OVD_COLOR_DEPTH_NONE,
      "rule order, not entry order" },
    { { "NO_COLOR=" }, false, OVD_COLOR_DEPTH_16, "empty NO_COLOR is ignored" },
    { { "NO_COLOR=", "COLORTERM=truecolor" }, false, OVD_COLOR_DEPTH_TRUECOLOR,
      "empty NO_COLOR falls through" },

    /* 2. COLORTERM, exactly truecolor or 24bit. */
    { { "COLORTERM=truecolor" }, false, OVD_COLOR_DEPTH_TRUECOLOR, "COLORTERM truecolor" },
    { { "COLORTERM=24bit" }, false, OVD_COLOR_DEPTH_TRUECOLOR, "COLORTERM 24bit" },
    { { "COLORTERM=TRUECOLOR" }, false, OVD_COLOR_DEPTH_16, "case-sensitive" },
    { { "COLORTERM=truecolor " }, false, OVD_COLOR_DEPTH_16, "exact, no trailing space" },
    { { "COLORTERM=24" }, false, OVD_COLOR_DEPTH_16, "exact, no prefix" },
    { { "COLORTERM=yes" }, false, OVD_COLOR_DEPTH_16, "other values" },
    { { "COLORTERM=" }, false, OVD_COLOR_DEPTH_16, "empty COLORTERM" },
    { { "COLORTERM=truecolor", "TERM=dumb" }, false, OVD_COLOR_DEPTH_TRUECOLOR,
      "COLORTERM beats TERM=dumb" },

    /* 3. Windows virtual-terminal output. */
    { {}, true, OVD_COLOR_DEPTH_TRUECOLOR, "virtual terminal" },
    { { "TERM=dumb" }, true, OVD_COLOR_DEPTH_TRUECOLOR, "virtual terminal beats TERM=dumb" },
    { { "TERM=xterm-256color" }, true, OVD_COLOR_DEPTH_TRUECOLOR,
      "virtual terminal beats 256color" },
    { { "COLORTERM=yes" }, true, OVD_COLOR_DEPTH_TRUECOLOR, "unmatched COLORTERM falls through" },

    /* 4. TERM containing 256color. */
    { { "TERM=xterm-256color" }, false, OVD_COLOR_DEPTH_256, "xterm-256color" },
    { { "TERM=screen-256color-bce" }, false, OVD_COLOR_DEPTH_256, "contains, not suffix" },
    { { "TERM=256color" }, false, OVD_COLOR_DEPTH_256, "the whole value" },
    { { "TERM=dumb-256color" }, false, OVD_COLOR_DEPTH_256, "rule 4 before rule 5" },
    { { "TERM=xterm-256colour" }, false, OVD_COLOR_DEPTH_16, "256colour does not contain it" },
    { { "TERM=xterm-256COLOR" }, false, OVD_COLOR_DEPTH_16, "case-sensitive" },

    /* 5. TERM exactly dumb. */
    { { "TERM=dumb" }, false, OVD_COLOR_DEPTH_NONE, "TERM=dumb" },
    { { "TERM=DUMB" }, false, OVD_COLOR_DEPTH_16, "case-sensitive" },
    { { "TERM=dumber" }, false, OVD_COLOR_DEPTH_16, "exact, no prefix match" },
    { { "TERM=xdumb" }, false, OVD_COLOR_DEPTH_16, "exact, no suffix match" },

    /* Variables other than the three are never read. */
    { { "FORCE_COLOR=1", "CLICOLOR_FORCE=1" }, false, OVD_COLOR_DEPTH_16, "other variables" },
};

static bool test_rules(void)
{
    bool passed = true;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        FakeEnvironment environment = {};
        memcpy(environment.entries, cases[i].entries, sizeof environment.entries);
        const OvdColorDepth got =
            ovd_detect_color_depth(fake_lookup, &environment, cases[i].virtual_terminal);
        if (got != cases[i].expected) {
            fprintf(stderr, "case %zu (%s): expected %s, got %s\n", i, cases[i].why,
                    depth_name(cases[i].expected), depth_name(got));
            passed = false;
        }
    }
    return passed;
}

/* Only NO_COLOR, COLORTERM, and TERM are looked up, each with the
 * caller's context, and each at most once. */
static bool test_lookups(void)
{
    FakeEnvironment environment = {};
    CHECK(ovd_detect_color_depth(fake_lookup, &environment, false) == OVD_COLOR_DEPTH_16);
    CHECK(environment.asked_count >= 1);
    CHECK(environment.asked_count <= 3);
    for (size_t i = 0; i < environment.asked_count; ++i) {
        const char *name = environment.asked[i];
        CHECK(name != nullptr);
        CHECK(strcmp(name, "NO_COLOR") == 0 || strcmp(name, "COLORTERM") == 0
              || strcmp(name, "TERM") == 0);
        for (size_t j = 0; j < i; ++j) {
            CHECK(strcmp(name, environment.asked[j]) != 0);
        }
    }
    return true;
}

/* A nullptr lookup is an empty environment. */
static bool test_null_lookup(void)
{
    CHECK(ovd_detect_color_depth(nullptr, nullptr, false) == OVD_COLOR_DEPTH_16);
    CHECK(ovd_detect_color_depth(nullptr, nullptr, true) == OVD_COLOR_DEPTH_TRUECOLOR);
    return true;
}

/* ---- The platform's real lookup -------------------------------------- */

/* CTest sets OVD_TEST_DETECT_PROBE=probe-value and NO_COLOR=1, and unsets
 * OVD_TEST_DETECT_UNSET, for this test only (tests/CMakeLists.txt). */
static bool test_platform_environment(void)
{
    const char *probe = ovd_platform_environment(nullptr, "OVD_TEST_DETECT_PROBE");
    if (probe == nullptr) {
        fputs("OVD_TEST_DETECT_PROBE is not set: run this test through ctest\n", stderr);
        return false;
    }
    CHECK(strcmp(probe, "probe-value") == 0);
    int context = 0;
    const char *again = ovd_platform_environment(&context, "OVD_TEST_DETECT_PROBE");
    CHECK(again != nullptr && strcmp(again, "probe-value") == 0);
    CHECK(ovd_platform_environment(nullptr, "OVD_TEST_DETECT_UNSET") == nullptr);
    CHECK(ovd_platform_environment(nullptr, nullptr) == nullptr);

    /* Detection through the real lookup: NO_COLOR=1 wins over the rest. */
    CHECK(ovd_detect_color_depth(ovd_platform_environment, nullptr, true)
          == OVD_COLOR_DEPTH_NONE);
    return true;
}

/* ---- Platform calls that need no terminal ---------------------------- */

static bool test_platform_arguments(void)
{
    CHECK(ovd_platform_enter(nullptr, false, false) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_enter(nullptr, true, true) == OVD_ERROR_INVALID_ARGUMENT);

    /* A record that already holds a step is refused, unchanged, with or
     * without input. */
    OvdPlatformModes held = {};
    held.steps = 1;
    held.saved[0] = 0x5a;
    CHECK(ovd_platform_enter(&held, false, false) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_enter(&held, true, true) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(held.steps == 1);
    CHECK(held.saved[0] == 0x5a);

    /* Input calls need step 4b recorded (002): refused without it, and for
     * nullptr or a capacity below the minimum. */
    OvdPlatformModes none = {};
    uint32_t ready = 0;
    char bytes[OVD_PLATFORM_READ_MIN];
    size_t length = 0;
    bool resized = false;
    CHECK(ovd_platform_input_wait(nullptr, 0, &ready) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_input_wait(&none, 0, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_input_wait(&none, 0, &ready) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_input_read(&none, bytes, sizeof bytes, &length, &resized)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_input_read(nullptr, bytes, sizeof bytes, &length, &resized)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_input_read(&none, nullptr, sizeof bytes, &length, &resized)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_input_read(&none, bytes, sizeof bytes - 1, &length, &resized)
          == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(none.steps == 0);

    /* The clock never runs backwards. */
    const uint64_t first = ovd_platform_clock_ms();
    CHECK(ovd_platform_clock_ms() >= first);

    /* Nothing recorded: leave does nothing, twice, and nullptr is safe. */
    OvdPlatformModes empty = {};
    CHECK(!ovd_platform_virtual_terminal(&empty));
    CHECK(!ovd_platform_virtual_terminal(nullptr));
    ovd_platform_leave(&empty);
    ovd_platform_leave(&empty);
    ovd_platform_leave(nullptr);
    CHECK(empty.steps == 0);

    CHECK(ovd_platform_size(nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    CHECK(ovd_platform_write(nullptr, 0));
    CHECK(ovd_platform_write("", 0));
    CHECK(!ovd_platform_write(nullptr, 1));
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "rules", test_rules },
        { "lookups", test_lookups },
        { "null_lookup", test_null_lookup },
        { "platform_environment", test_platform_environment },
        { "platform_arguments", test_platform_arguments },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (tests[i].run()) {
            printf("ok   %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    puts("detect: R10/R36 rules, lookups, real environment, and platform arguments passed");
    return EXIT_SUCCESS;
}
