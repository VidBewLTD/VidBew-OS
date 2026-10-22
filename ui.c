/* ui.c -- classic 3D control drawing helpers. */

#include "ui.h"
#include "kstd.h"

void ui_panel(surface_t *s, int x, int y, int w, int h, int sunken)
{
    s_fill(s, x, y, w, h, COL_LIGHT_GREY);
    s_bevel(s, x, y, w, h, sunken);
}

void ui_text_center(surface_t *s, int x, int y, int w, const char *text, int fg)
{
    int tw = s_text_width(text);
    s_text(s, x + (w - tw) / 2, y, text, fg, COL_NONE);
}

void ui_button(surface_t *s, int x, int y, int w, int h, const char *label, int pressed, int disabled)
{
    s_fill(s, x, y, w, h, COL_LIGHT_GREY);
    s_bevel(s, x, y, w, h, pressed);
    int tw = s_text_width(label);
    int tx = x + (w - tw) / 2 + (pressed ? 1 : 0);
    int ty = y + (h - FONT_HEIGHT) / 2 + (pressed ? 1 : 0);
    if (disabled) {
        s_text(s, tx + 1, ty + 1, label, COL_WHITE, COL_NONE);
        s_text(s, tx, ty, label, COL_DARK_GREY, COL_NONE);
    } else {
        s_text(s, tx, ty, label, COL_BLACK, COL_NONE);
    }
}

void ui_checkbox(surface_t *s, int x, int y, const char *label, int checked, int disabled)
{
    s_fill(s, x, y, 13, 13, disabled ? COL_LIGHT_GREY : COL_WHITE);
    s_bevel1(s, x, y, 13, 13, 1);
    if (checked) {
        int c = disabled ? COL_DARK_GREY : COL_BLACK;
        s_line(s, x + 3, y + 6, x + 5, y + 8, c);
        s_line(s, x + 3, y + 7, x + 5, y + 9, c);
        s_line(s, x + 5, y + 8, x + 10, y + 3, c);
        s_line(s, x + 5, y + 9, x + 10, y + 4, c);
    }
    s_text(s, x + 20, y - 2, label, disabled ? COL_DARK_GREY : COL_BLACK, COL_NONE);
}

void ui_radio(surface_t *s, int x, int y, const char *label, int selected)
{
    s_fcircle(s, x + 6, y + 6, 6, COL_WHITE);
    s_circle(s, x + 6, y + 6, 6, COL_DARK_GREY);
    if (selected) s_fcircle(s, x + 6, y + 6, 2, COL_BLACK);
    s_text(s, x + 20, y - 2, label, COL_BLACK, COL_NONE);
}

void ui_groupbox(surface_t *s, int x, int y, int w, int h, const char *title)
{
    s_rect(s, x, y + 8, w, h - 8, COL_DARK_GREY);
    s_rect(s, x + 1, y + 9, w, h - 8, COL_WHITE);
    /* the white offset rectangle should not cross the dark one's top-left */
    s_hline(s, x, y + 8, w, COL_DARK_GREY);
    int tw = s_text_width(title);
    s_fill(s, x + 8, y, tw + 8, 16, COL_LIGHT_GREY);
    s_text(s, x + 12, y, title, COL_BLACK, COL_NONE);
}

void ui_textfield(surface_t *s, int x, int y, int w, const char *text, int focused, int disabled)
{
    s_fill(s, x, y, w, 22, disabled ? COL_LIGHT_GREY : COL_WHITE);
    s_bevel(s, x, y, w, 22, 1);
    int maxc = (w - 8) / FONT_WIDTH;
    int len = (int)kstrlen(text);
    int start = len > maxc ? len - maxc : 0;
    s_text(s, x + 4, y + 3, text + start, disabled ? COL_DARK_GREY : COL_BLACK, COL_NONE);
    if (focused) s_vline(s, x + 4 + (len - start) * FONT_WIDTH, y + 3, 16, COL_BLACK);
}

void ui_slider(surface_t *s, int x, int y, int w, int min, int max, int val)
{
    s_fill(s, x, y + 6, w, 4, COL_LIGHT_GREY);
    s_bevel1(s, x, y + 6, w, 4, 1);
    int range = max - min;
    if (range <= 0) range = 1;
    if (val < min) val = min;
    if (val > max) val = max;
    int kx = x + (w - 10) * (val - min) / range;
    s_fill(s, kx, y, 10, 16, COL_LIGHT_GREY);
    s_bevel(s, kx, y, 10, 16, 0);
}

int ui_slider_value(int x, int w, int min, int max, int mouse_x)
{
    int v = min + ((mouse_x - x - 5) * (max - min) + (w - 10) / 2) / (w - 10);
    if (v < min) v = min;
    if (v > max) v = max;
    return v;
}

void ui_progress(surface_t *s, int x, int y, int w, int h, int percent)
{
    s_fill(s, x, y, w, h, COL_WHITE);
    s_bevel1(s, x, y, w, h, 1);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_fill(s, x + 2, y + 2, (w - 4) * percent / 100, h - 4, COL_BLUE);
}

void ui_tab(surface_t *s, int x, int y, int w, const char *label, int active)
{
    int top = active ? y : y + 2;
    s_fill(s, x, top, w, 22 - (top - y) + 1, COL_LIGHT_GREY);
    s_hline(s, x + 1, top, w - 2, COL_WHITE);
    s_vline(s, x, top + 1, 22 - (top - y), COL_WHITE);
    s_vline(s, x + w - 1, top + 1, 22 - (top - y), COL_BLACK);
    s_vline(s, x + w - 2, top + 1, 22 - (top - y), COL_DARK_GREY);
    ui_text_center(s, x, top + 3, w, label, COL_BLACK);
}

/* ---------------- scrollbar ---------------- */

static void arrow(surface_t *s, int cx, int cy, int up, int color)
{
    for (int i = 0; i < 4; i++) {
        int w = up ? i : 3 - i;
        s_hline(s, cx - w, cy + (up ? i : i), 2 * w + 1, color);
    }
}

static void thumb_geometry(int h, int total, int visible, int pos, int *ty, int *th)
{
    int track = h - 2 * UI_SCROLL_W;
    int thumb = total > 0 ? track * visible / total : track;
    if (thumb < 16) thumb = 16;
    if (thumb > track) thumb = track;
    int range = total - visible;
    *th = thumb;
    *ty = range > 0 ? (track - thumb) * pos / range : 0;
}

void ui_vscroll(surface_t *s, int x, int y, int h, int total, int visible, int pos)
{
    s_fill(s, x, y, UI_SCROLL_W, h, COL_LIGHT_GREY);
    for (int yy = y + UI_SCROLL_W; yy < y + h - UI_SCROLL_W; yy += 2)
        for (int xx = x + (yy & 2 ? 1 : 0); xx < x + UI_SCROLL_W; xx += 2) s_pixel(s, xx, yy, COL_WHITE);
    s_fill(s, x, y, UI_SCROLL_W, UI_SCROLL_W, COL_LIGHT_GREY);
    s_bevel(s, x, y, UI_SCROLL_W, UI_SCROLL_W, 0);
    arrow(s, x + 8, y + 5, 1, COL_BLACK);
    s_fill(s, x, y + h - UI_SCROLL_W, UI_SCROLL_W, UI_SCROLL_W, COL_LIGHT_GREY);
    s_bevel(s, x, y + h - UI_SCROLL_W, UI_SCROLL_W, UI_SCROLL_W, 0);
    arrow(s, x + 8, y + h - UI_SCROLL_W + 6, 0, COL_BLACK);
    if (total > visible) {
        int ty, th;
        thumb_geometry(h, total, visible, pos, &ty, &th);
        s_fill(s, x, y + UI_SCROLL_W + ty, UI_SCROLL_W, th, COL_LIGHT_GREY);
        s_bevel(s, x, y + UI_SCROLL_W + ty, UI_SCROLL_W, th, 0);
    }
}

int ui_vscroll_click(int x, int y, int h, int total, int visible, int pos, int mx, int my)
{
    if (mx < x || mx >= x + UI_SCROLL_W || my < y || my >= y + h) return -1;
    int range = total - visible;
    if (range < 0) range = 0;
    int np = pos;
    if (my < y + UI_SCROLL_W) np = pos - 1;
    else if (my >= y + h - UI_SCROLL_W) np = pos + 1;
    else if (range > 0) {
        int ty, th;
        thumb_geometry(h, total, visible, pos, &ty, &th);
        int track = h - 2 * UI_SCROLL_W;
        int want = my - (y + UI_SCROLL_W) - th / 2;      /* thumb centred on the click */
        if (track - th > 0) np = want * range / (track - th);
    }
    if (np < 0) np = 0;
    if (np > range) np = range;
    return np;
}
