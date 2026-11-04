/* Non-interactive run of the system backend (T034): FR-010, FR-011;
 * research.md R28 step 1, R44's check order.
 *
 * The CMake driver (tests/CMakeLists.txt) runs this program with stdout
 * redirected to a file and fails unless it exits 0 and the file is empty,
 * so "writing nothing" is checked outside the program. This program checks
 * the results through the public API and prints only to stderr. */
#include "support/test_alloc.h"

#include <overdrive/overdrive.h>

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

/* create returns `expected`, leaves *out as it was, and calls no
 * allocator. */
static bool creates(const OvdTerminalConfig *config, OvdResult expected, const TestAlloc *alloc)
{
    OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
    OvdTerminal *out = sentinel;
    const OvdResult result = ovd_terminal_create(config, &out);
    if (result != expected) {
        fprintf(stderr, "create returned %s, expected %s\n", ovd_result_name(result),
                ovd_result_name(expected));
    }
    CHECK(result == expected);
    CHECK(out == sentinel);
    CHECK(alloc->allocate_calls == 0);
    return true;
}

/* stdout is not a terminal: every system config is refused before any
 * mode, write, or allocation (R28 step 1). */
static bool test_not_a_terminal(void)
{
    TestAlloc alloc;
    test_alloc_init(&alloc);
    const OvdAllocator allocator = test_alloc_allocator(&alloc);

    OvdTerminalConfig c = ovd_terminal_config_default();
    CHECK(c.backend == OVD_BACKEND_SYSTEM);
    c.allocator = &allocator;
    CHECK(creates(&c, OVD_ERROR_NOT_A_TERMINAL, &alloc));

    /* Every combination of the system-only options. */
    for (unsigned bits = 0; bits < 8; ++bits) {
        c = ovd_terminal_config_default();
        c.allocator = &allocator;
        c.detect_color_depth = (bits & 1u) != 0;
        c.alternate_screen = (bits & 2u) != 0;
        c.synchronized_output = (bits & 4u) != 0;
        CHECK(creates(&c, OVD_ERROR_NOT_A_TERMINAL, &alloc));
    }

    /* Headless-only fields are not validated for the system backend. */
    c = ovd_terminal_config_default();
    c.allocator = &allocator;
    c.headless_size = (OvdSize){ 0, 0 };
    CHECK(creates(&c, OVD_ERROR_NOT_A_TERMINAL, &alloc));

    /* The default allocator. */
    c = ovd_terminal_config_default();
    OvdTerminal *sentinel = (OvdTerminal *)&sentinel;
    OvdTerminal *out = sentinel;
    CHECK(ovd_terminal_create(&c, &out) == OVD_ERROR_NOT_A_TERMINAL);
    CHECK(out == sentinel);

    CHECK(alloc.violations == 0);
    return true;
}

/* Argument errors are reported before the terminal check (R44). */
static bool test_arguments_first(void)
{
    TestAlloc alloc;
    test_alloc_init(&alloc);
    OvdAllocator allocator = test_alloc_allocator(&alloc);

    OvdTerminalConfig c = ovd_terminal_config_default();
    c.allocator = &allocator;
    c.color_depth = (OvdColorDepth)4;
    CHECK(creates(&c, OVD_ERROR_INVALID_ARGUMENT, &alloc));

    c = ovd_terminal_config_default();
    allocator.release = nullptr;
    c.allocator = &allocator;
    CHECK(creates(&c, OVD_ERROR_INVALID_ARGUMENT, &alloc));

    c = ovd_terminal_config_default();
    CHECK(ovd_terminal_create(&c, nullptr) == OVD_ERROR_INVALID_ARGUMENT);
    return true;
}

int main(void)
{
    bool ok = true;
    ok = test_not_a_terminal() && ok;
    ok = test_arguments_first() && ok;
    /* Nothing to restore, so nothing is written. */
    ovd_terminal_destroy(nullptr);
    if (!ok) {
        fprintf(stderr, "test_system_notty: FAILED\n");
        return EXIT_FAILURE;
    }
    fprintf(stderr, "test_system_notty: all checks passed\n");
    return EXIT_SUCCESS;
}
