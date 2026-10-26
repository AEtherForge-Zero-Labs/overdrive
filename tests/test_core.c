#include <overdrive/core.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return EXIT_FAILURE; \
        } \
    } while (false)

int main(void)
{
    const struct {
        OvdResult result;
        const char *name;
    } cases[] = {
        { OVD_OK, "OVD_OK" },
        { OVD_ERROR_INVALID_ARGUMENT, "OVD_ERROR_INVALID_ARGUMENT" },
        { OVD_ERROR_ALLOCATION, "OVD_ERROR_ALLOCATION" },
        { OVD_ERROR_STATE, "OVD_ERROR_STATE" },
        { OVD_ERROR_UNSUPPORTED, "OVD_ERROR_UNSUPPORTED" },
        { OVD_ERROR_NOT_A_TERMINAL, "OVD_ERROR_NOT_A_TERMINAL" },
        { OVD_ERROR_PLATFORM, "OVD_ERROR_PLATFORM" },
        { OVD_ERROR_IO, "OVD_ERROR_IO" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *name = ovd_result_name(cases[i].result);
        CHECK(name != nullptr);
        CHECK(strcmp(name, cases[i].name) == 0);
        CHECK(name == ovd_result_name(cases[i].result));
    }

    /* Deliberately cross the closed enum boundary to test defensive lookup. */
    CHECK(strcmp(ovd_result_name((OvdResult)-1), "OVD_RESULT_UNKNOWN") == 0);
    CHECK(strcmp(ovd_result_name((OvdResult)INT32_MIN), "OVD_RESULT_UNKNOWN") == 0);
    CHECK(strcmp(ovd_result_name((OvdResult)INT32_MAX), "OVD_RESULT_UNKNOWN") == 0);

    const OvdVersion version = ovd_version();
    CHECK(version.major == OVD_VERSION_MAJOR);
    CHECK(version.minor == OVD_VERSION_MINOR);
    CHECK(version.patch == OVD_VERSION_PATCH);
    puts("core: all result names, unknown values, and runtime version passed");
    return EXIT_SUCCESS;
}
