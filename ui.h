#ifndef UI_H
#define UI_H

/* ui.h -- small drawing helpers for classic 3D controls. They only draw;
 * apps keep their own state and hit-test with ui_inside(). */

#include "gfx.h"

static inline int ui_inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

void ui_panel(surface_t *s, int x, int y, int w, int h, int sunken);
void ui_button(surface_t *s, int x, int y, int w, int h, const char *label, int pressed, int disabled);
void ui_checkbox(surface_t *s, int x, int y, const char *label, int checked, int disabled);
void ui_radio(surface_t *s, int x, int y, const char *label, int selected);
void ui_groupbox(surface_t *s, int x, int y, int w, int h, const char *title);
void ui_textfield(surface_t *s, int x, int y, int w, const char *text, int focused, int disabled);
void ui_slider(surface_t *s, int x, int y, int w, int min, int max, int val);
int  ui_slider_value(int x, int w, int min, int max, int mouse_x);       /* value under the mouse */
void ui_progress(surface_t *s, int x, int y, int w, int h, int percent);
void ui_text_center(surface_t *s, int x, int y, int w, const char *text, int fg);
void ui_tab(surface_t *s, int x, int y, int w, const char *label, int active);

/* vertical scrollbar, 16px wide. Returns the new scroll position for a click
 * at (mx,my), or -1 if the click missed it. */
#define UI_SCROLL_W 16
void ui_vscroll(surface_t *s, int x, int y, int h, int total, int visible, int pos);
int  ui_vscroll_click(int x, int y, int h, int total, int visible, int pos, int mx, int my);

#endif
