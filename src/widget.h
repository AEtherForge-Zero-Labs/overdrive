/* Pieces shared by the widgets (specs/004-widgets, R309 and R310).
 * Internal to Overdrive. Transformational (Art. IV): the clip helpers
 * change only the canvas the caller passes, and restore it. */
#ifndef OVERDRIVE_SRC_WIDGET_H
#define OVERDRIVE_SRC_WIDGET_H

#include <overdrive/render.h>
#include <overdrive/widgets.h>

/* Sets the canvas clip to area intersected with the current clip, and
 * returns the clip it replaced (R309). ovd_canvas_set_clip intersects
 * only with the canvas bounds, so this narrows first: a widget never
 * draws outside a clip its caller set. An empty intersection, or a
 * non-positive area, leaves a clip that discards every cell. A nullptr
 * canvas does nothing and returns {0, 0, 0, 0}. */
[[nodiscard]] OvdRect ovd_widget_clip_enter(OvdCanvas *canvas, OvdRect area);

/* Restores the clip ovd_widget_clip_enter returned. */
void ovd_widget_clip_leave(OvdCanvas *canvas, OvdRect previous);

/* base with overlay laid over it (R310): the attrs are OR'ed, and each of
 * overlay's colors that is not OVD_COLOR_KIND_DEFAULT replaces base's. A
 * red cell under the default selected style stays red and gains
 * OVD_ATTR_REVERSE. */
[[nodiscard]] OvdStyle ovd_widget_style_combine(OvdStyle base, OvdStyle overlay);

#endif /* OVERDRIVE_SRC_WIDGET_H */
