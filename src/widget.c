/* Pieces shared by the widgets: widget.h, and two public functions of
 * widgets.h (R311, SC-001). */
#include "widget.h"

OvdWidgetStyle ovd_widget_style_default(void)
{
    const OvdStyle plain = OVD_STYLE_DEFAULT;
    OvdStyle selected = plain;
    selected.attrs = OVD_ATTR_REVERSE;
    OvdStyle header = plain;
    header.attrs = OVD_ATTR_BOLD;
    OvdStyle empty = plain;
    empty.attrs = OVD_ATTR_DIM;
    return (OvdWidgetStyle){
        .normal = plain,
        .selected = selected,
        .header = header,
        .empty = empty,
        .cursor = selected,
    };
}

OvdText ovd_text_array_item(void *context, int32_t index)
{
    if (context == nullptr || index < 0) {
        return (OvdText){ nullptr, 0 };
    }
    const char *const *entries = context;
    return ovd_text(entries[index]);
}

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

OvdRect ovd_widget_clip_enter(OvdCanvas *canvas, OvdRect area)
{
    const OvdRect previous = ovd_canvas_clip(canvas);
    if (canvas == nullptr) {
        return previous;
    }
    /* The current clip lies within the canvas, so the intersection's
     * fields fit in int32_t; computed in 64 bits so that area's far edges
     * cannot overflow. */
    const int64_t x0 = max64(area.x, previous.x);
    const int64_t y0 = max64(area.y, previous.y);
    const int64_t x1 = min64((int64_t)area.x + area.cols, (int64_t)previous.x + previous.cols);
    const int64_t y1 = min64((int64_t)area.y + area.rows, (int64_t)previous.y + previous.rows);
    OvdRect clip = { 0, 0, 0, 0 };
    if (area.cols > 0 && area.rows > 0 && x1 > x0 && y1 > y0) {
        clip = (OvdRect){ (int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0), (int32_t)(y1 - y0) };
    }
    ovd_canvas_set_clip(canvas, clip);
    return previous;
}

void ovd_widget_clip_leave(OvdCanvas *canvas, OvdRect previous)
{
    if (canvas != nullptr) {
        ovd_canvas_set_clip(canvas, previous);
    }
}

OvdStyle ovd_widget_style_combine(OvdStyle base, OvdStyle overlay)
{
    OvdStyle out = base;
    out.attrs = (OvdAttr)(base.attrs | overlay.attrs);
    if (overlay.fg.kind == OVD_COLOR_KIND_INDEXED || overlay.fg.kind == OVD_COLOR_KIND_RGB) {
        out.fg = overlay.fg;
    }
    if (overlay.bg.kind == OVD_COLOR_KIND_INDEXED || overlay.bg.kind == OVD_COLOR_KIND_RGB) {
        out.bg = overlay.bg;
    }
    return out;
}
