#include <overdrive/core.h>

OVD_API OvdVersion ovd_version(void)
{
    return (OvdVersion){
        .major = OVD_VERSION_MAJOR,
        .minor = OVD_VERSION_MINOR,
        .patch = OVD_VERSION_PATCH,
    };
}

OVD_API const char *ovd_result_name(OvdResult result)
{
    switch (result) {
    case OVD_OK:
        return "OVD_OK";
    case OVD_ERROR_INVALID_ARGUMENT:
        return "OVD_ERROR_INVALID_ARGUMENT";
    case OVD_ERROR_ALLOCATION:
        return "OVD_ERROR_ALLOCATION";
    case OVD_ERROR_STATE:
        return "OVD_ERROR_STATE";
    case OVD_ERROR_UNSUPPORTED:
        return "OVD_ERROR_UNSUPPORTED";
    case OVD_ERROR_NOT_A_TERMINAL:
        return "OVD_ERROR_NOT_A_TERMINAL";
    case OVD_ERROR_PLATFORM:
        return "OVD_ERROR_PLATFORM";
    case OVD_ERROR_IO:
        return "OVD_ERROR_IO";
    default:
        return "OVD_RESULT_UNKNOWN";
    }
}
