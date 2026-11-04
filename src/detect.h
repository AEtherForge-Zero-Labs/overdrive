/*
 * Color-depth detection from the environment (FR-013; research R10, R36).
 *
 * Internal to Overdrive: nothing here is exported. Transformational: the
 * function reads the environment only through the lookup it is given,
 * keeps no state, and never allocates. The system backend uses it unless
 * the caller turned detection off; the headless backend never does (R35).
 * Platform-independent: whether Windows virtual-terminal output is on is
 * passed in, not tested here.
 */
#ifndef OVERDRIVE_SRC_DETECT_H
#define OVERDRIVE_SRC_DETECT_H

#include <overdrive/terminal.h>

/* Returns the value of the environment variable `name`, or nullptr if it
 * is not set. The string must stay valid until the call that asked for it
 * returns. ovd_platform_environment (platform/platform.h) is the real one;
 * tests inject their own. */
typedef const char *(*OvdEnvironmentLookup)(void *context, const char *name);

/* Returns the color depth the environment asks for. The first matching
 * rule wins, in this order (R10, R36):
 * 1. NO_COLOR is set and non-empty → OVD_COLOR_DEPTH_NONE (no-color.org).
 * 2. COLORTERM is exactly "truecolor" or "24bit" → OVD_COLOR_DEPTH_TRUECOLOR.
 * 3. virtual_terminal is true → OVD_COLOR_DEPTH_TRUECOLOR. It is true when
 *    the system backend enabled Windows virtual-terminal output
 *    (ovd_platform_virtual_terminal).
 * 4. TERM contains "256color" → OVD_COLOR_DEPTH_256.
 * 5. TERM is exactly "dumb" → OVD_COLOR_DEPTH_NONE.
 * 6. Otherwise → OVD_COLOR_DEPTH_16.
 * Comparisons are byte-exact and case-sensitive. Only NO_COLOR,
 * COLORTERM, and TERM are looked up, each with `context`. A nullptr
 * lookup is an empty environment. */
[[nodiscard]] OvdColorDepth ovd_detect_color_depth(OvdEnvironmentLookup lookup, void *context,
                                                   bool virtual_terminal);

#endif /* OVERDRIVE_SRC_DETECT_H */
