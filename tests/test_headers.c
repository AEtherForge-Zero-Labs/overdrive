#include <overdrive/overdrive.h>
#include <overdrive/overdrive.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert(sizeof(OvdResult) == sizeof(int32_t));
static_assert(sizeof(OvdBackend) == sizeof(uint8_t));
static_assert(sizeof(OvdColorDepth) == sizeof(uint8_t));
static_assert(sizeof(OvdColorKind) == sizeof(uint8_t));
static_assert(sizeof(OvdBorder) == sizeof(uint8_t));
static_assert(sizeof(OvdAttr) == sizeof(uint16_t));
static_assert(sizeof(OvdOutcome) == sizeof(uint8_t));
static_assert(sizeof(OvdAlign) == sizeof(uint8_t));
static_assert(sizeof(OvdColumnSizing) == sizeof(uint8_t));

int main(void)
{
    const uint32_t orange = 0xFF9138;
    const OvdColor rgb = ovd_color_rgb(255, 145, 56);
    const OvdColor hex = ovd_color_hex(orange);
    const OvdColor indexed = ovd_color_indexed(255);
    const OvdStyle style = OVD_STYLE_DEFAULT;
    const OvdText abc = ovd_text("abc");
    const OvdText none = ovd_text(nullptr);
    OvdTerminal *terminal = nullptr;
    if (abc.length != 3 || memcmp(abc.bytes, "abc", 3) != 0 || none.bytes != nullptr
            || none.length != 0) {
        fputs("ovd_text failed\n", stderr);
        return EXIT_FAILURE;
    }
    if (terminal != nullptr || rgb.kind != OVD_COLOR_KIND_RGB
            || hex.r != rgb.r || hex.g != rgb.g || hex.b != rgb.b
            || indexed.kind != OVD_COLOR_KIND_INDEXED || indexed.index != 255
            || style.fg.kind != OVD_COLOR_KIND_DEFAULT
            || style.bg.kind != OVD_COLOR_KIND_DEFAULT || style.attrs != OVD_ATTR_NONE) {
        fputs("public header values or inline functions failed\n", stderr);
        return EXIT_FAILURE;
    }
    puts("headers: C23 types, public value helpers, and ovd_text passed");
    return EXIT_SUCCESS;
}
