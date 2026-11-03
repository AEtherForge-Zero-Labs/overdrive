#include "detect.h"

#include <string.h>

/* The value of `name`, or nullptr if it is unset or there is no lookup. */
static const char *lookup_variable(OvdEnvironmentLookup lookup, void *context, const char *name)
{
    return lookup == nullptr ? nullptr : lookup(context, name);
}

static bool equals(const char *value, const char *expected)
{
    return value != nullptr && strcmp(value, expected) == 0;
}

OvdColorDepth ovd_detect_color_depth(OvdEnvironmentLookup lookup, void *context,
                                     bool virtual_terminal)
{
    /* R10 and R36, first match wins. */
    const char *no_color = lookup_variable(lookup, context, "NO_COLOR");
    if (no_color != nullptr && no_color[0] != '\0') {
        return OVD_COLOR_DEPTH_NONE;
    }
    const char *colorterm = lookup_variable(lookup, context, "COLORTERM");
    if (equals(colorterm, "truecolor") || equals(colorterm, "24bit")) {
        return OVD_COLOR_DEPTH_TRUECOLOR;
    }
    if (virtual_terminal) {
        return OVD_COLOR_DEPTH_TRUECOLOR;
    }
    const char *term = lookup_variable(lookup, context, "TERM");
    if (term != nullptr && strstr(term, "256color") != nullptr) {
        return OVD_COLOR_DEPTH_256;
    }
    if (equals(term, "dumb")) {
        return OVD_COLOR_DEPTH_NONE;
    }
    return OVD_COLOR_DEPTH_16;
}
