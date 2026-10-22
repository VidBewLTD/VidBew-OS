/* app_control.c -- the Control Panel and its Display, Input and Sound applets.
 * (Network, Date & Time and System live in app_applets.c.)
 *
 * Every applet is its own task and its own window, so you can have several
 * open while the rest of the desktop keeps running. */

#include "apps.h"
#include "ui.h"
#include "kstd.h"
#include "heap.h"
#include "task.h"
#include "timer.h"
#include "keyboard.h"
#include "input_mode.h"
#include "mouse.h"
#include "sb16.h"
#include "audio.h"
#include "settings.h"

/* ---------------------------------------------------------------- Control Panel */

#define CP_CELL_W 120
#define CP_CELL_H 96

typedef struct { const char *label; icon_type_t icon; int app; } cp_item_t;
static const cp_item_t cp_items[] = {
    { "Display",     ICON_DISPLAY,  APP_DISPLAY },
    { "Network",     ICON_NETWORK,  APP_NETWORK },
    { "Input",       ICON_POINTER,  APP_INPUT },
    { "Sound",       ICON_DISC,     APP_SOUND },
    { "Date & Time", ICON_CALENDAR, APP_DATETIME },
    { "System",      ICON_SYSTEM,   APP_SYSTEM },
};
#define CP_N ((int)(sizeof(cp_items) / sizeof(cp_items[0])))

static const menu_item_t cp_file[] = { { "Open", 1, 0, 0 } };
const app_def_t control_def = { "Control Panel", ICON_CONTROL, { 1, cp_file }, { 0, 0 }, { 0, 0 } };

static void cp_paint(window_t *win, int sel)
{
    surface_t *s = wm_surface(win);
    int cw, ch;
    wm_client_size(win, &cw, &ch);
    if (s->w < cw || s->h < ch) return;
    s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
    int cols = cw / CP_CELL_W;
    if (cols < 1) cols = 1;
    for (int i = 0; i < CP_N; i++) {
        int x = (i % cols) * CP_CELL_W, y = (i / cols) * CP_CELL_H;
        icon_draw(s, x + (CP_CELL_W - 48) / 2, y + 8, 48, cp_items[i].icon);
        int tw = s_text_width(cp_items[i].label);
        int lx = x + (CP_CELL_W - tw) / 2, ly = y + 62;
        if (i == sel) { s_fill(s, lx - 2, ly - 1, tw + 4, 18, COL_BLUE); s_text(s, lx, ly, cp_items[i].label, COL_WHITE, COL_NONE); }
        else s_text(s, lx, ly, cp_items[i].label, COL_BLACK, COL_NONE);
    }
    wm_invalidate(win);
}

void app_control_main(void *arg)
{
    window_t *win = wm_create("Control Panel", 3 * CP_CELL_W, 2 * CP_CELL_H + 8, WF_RESIZABLE, &control_def);
    if (!win) return;
    wm_set_min_size(win, CP_CELL_W, CP_CELL_H);
    int sel = -1;
    cp_paint(win, sel);
    event_t ev;
    for (;;) {
        wm_next_event(win, &ev, -1);
        switch (ev.type) {
        case EV_CLOSE: wm_destroy(win); return;
        case EV_RESIZE: wm_resize_client(win, ev.a, ev.b); break;
        case EV_MOUSE_DOWN:
            if (ev.c == 1) {
                int cw, ch;
                wm_client_size(win, &cw, &ch);
                int cols = cw / CP_CELL_W;
                if (cols < 1) cols = 1;
                int col = ev.a / CP_CELL_W, row = ev.b / CP_CELL_H, idx = row * cols + col;
                sel = (col < cols && idx >= 0 && idx < CP_N) ? idx : -1;
                if (sel >= 0 && ev.d) app_launch(cp_items[sel].app);
            }
            break;
        case EV_MENU: if (ev.a == 1 && sel >= 0) app_launch(cp_items[sel].app); break;
        case EV_KEY: if (ev.a == '\n' && sel >= 0) app_launch(cp_items[sel].app); break;
        default: break;
        }
        cp_paint(win, sel);
    }
}

/* ---------------------------------------------------------------- Display applet */

const app_def_t display_def = { "Display", ICON_DISPLAY, { 0, 0 }, { 0, 0 }, { 0, 0 } };

typedef struct { int w, h; } res_t;
static const int depth_list[7] = { 1, 4, 8, 15, 16, 24, 32 };
static const char *depth_label[7] = { "Monochrome (1-bit)", "16 colors (4-bit)", "256 colors (8-bit)",
                                      "High Color (15-bit)", "High Color (16-bit)",
                                      "True Color (24-bit)", "True Color (32-bit)" };

static int mode_exists(int w, int h, int bpp)
{
    for (int i = 0; i < gfx_mode_count(); i++) {
        const vbe_mode_t *m = gfx_mode_at(i);
        if (m->w == w && m->h == h && m->bpp == bpp) return 1;
    }
    return 0;
}

static int build_resolutions(res_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < gfx_mode_count(); i++) {
        const vbe_mode_t *m = gfx_mode_at(i);
        int dup = 0;
        for (int k = 0; k < n; k++) if (out[k].w == m->w && out[k].h == m->h) dup = 1;
        if (dup || n >= max) continue;
        int pos = n;
        while (pos > 0 && out[pos - 1].w * out[pos - 1].h > m->w * m->h) { out[pos] = out[pos - 1]; pos--; }
        out[pos].w = m->w;
        out[pos].h = m->h;
        n++;
    }
    return n;
}

#define DSP_W 424
#define DSP_H 322

static void dsp_paint(window_t *win, res_t *res, int nres, int sel, int top, int bpp, int tab, int pressed, int scheme)
{
    surface_t *s = wm_surface(win);
    s_fill(s, 0, 0, DSP_W, DSP_H, COL_LIGHT_GREY);
    ui_tab(s, 8, 6, 92, "Settings", tab == 0);
    ui_tab(s, 100, 6, 100, "Appearance", tab == 1);
    s_bevel(s, 4, 28, DSP_W - 8, 238, 0);
    s_fill(s, 9, 28, tab == 0 ? 90 : 0, 2, COL_LIGHT_GREY);

    if (tab == 0) {
        s_text(s, 16, 40, "Screen area:", COL_BLACK, COL_NONE);
        s_fill(s, 16, 60, 176, 148, COL_WHITE);
        s_bevel(s, 16, 60, 176, 148, 1);
        int rows = 7;
        for (int i = 0; i < rows && top + i < nres; i++) {
            char b[32];
            ksnprintf(b, sizeof(b), "%d x %d", res[top + i].w, res[top + i].h);
            int y = 63 + i * 20, hot = (top + i == sel);
            if (hot) s_fill(s, 18, y, 176 - 4 - UI_SCROLL_W, 20, COL_BLUE);
            s_text(s, 24, y + 2, b, hot ? COL_WHITE : COL_BLACK, COL_NONE);
        }
        ui_vscroll(s, 16 + 176 - UI_SCROLL_W - 2, 62, 144, nres, rows, top);

        s_text(s, 214, 40, "Colors:", COL_BLACK, COL_NONE);
        int y = 62;
        for (int i = 0; i < 7; i++) {
            if (sel >= 0 && (mode_exists(res[sel].w, res[sel].h, depth_list[i]) || depth_list[i] == bpp)) {
                ui_radio(s, 214, y, depth_label[i], bpp == depth_list[i]);
                y += 24;
            }
        }
        char b[96];
        ksnprintf(b, sizeof(b), "Current: %d x %d, %s", SCREEN_WIDTH, SCREEN_HEIGHT, gfx_depth_name(gfx_cur_bpp()));
        s_text(s, 16, 216, b, COL_BLACK, COL_NONE);
        s_text(s, 16, 234, gfx_live_capable() ? "Changes apply immediately (you can revert)." : "Changes take effect after a restart.",
               COL_DARK_GREY, COL_NONE);
    } else {
        s_text(s, 16, 40, "Color scheme:", COL_BLACK, COL_NONE);
        for (int i = 0; i < wm_scheme_count(); i++) {
            int y = 62 + i * 30, d, t;
            wm_scheme_swatch(i, &d, &t);
            s_fill(s, 16, y, DSP_W - 40, 28, i == scheme ? COL_BLUE : COL_WHITE);
            s_bevel1(s, 16, y, DSP_W - 40, 28, 1);
            s_fill(s, 24, y + 5, 30, 18, d);
            s_rect(s, 24, y + 5, 30, 18, COL_BLACK);
            s_fill(s, 58, y + 5, 30, 18, t);
            s_rect(s, 58, y + 5, 30, 18, COL_BLACK);
            s_text(s, 98, y + 6, wm_scheme_name(i), i == scheme ? COL_WHITE : COL_BLACK, COL_NONE);
        }
    }
    ui_button(s, DSP_W - 184, 276, 84, 28, "Apply", pressed == 0, tab != 0);
    ui_button(s, DSP_W - 92, 276, 84, 28, "Close", pressed == 1, 0);
    wm_invalidate(win);
}

static void dsp_apply(int w, int h, int bpp)
{
    if (w == SCREEN_WIDTH && h == SCREEN_HEIGHT && bpp == gfx_cur_bpp()) return;
    int ow = SCREEN_WIDTH, oh = SCREEN_HEIGHT, ob = gfx_cur_bpp();
    if (gfx_live_capable()) {
        if (!wm_change_display(w, h, bpp)) {
            dlg_message("Display", "The display could not switch to that mode.", 0, 0);
            return;
        }
        int keep = dlg_message("Display Settings",
                               "Keep these display settings?\nReverting automatically in 15 seconds.", 2, 1500);
        if (keep == 0) {
            g_settings.disp_w = (u16)w; g_settings.disp_h = (u16)h; g_settings.disp_bpp = (u8)bpp;
            settings_save();
        } else {
            wm_change_display(ow, oh, ob);
        }
    } else {
        g_settings.disp_w = (u16)w; g_settings.disp_h = (u16)h; g_settings.disp_bpp = (u8)bpp;
        settings_save();
        if (dlg_message("Display Settings", "The new display mode is saved.\nRestart VidBew/OS now to use it?", 1, 0) == 0)
            wm_post_command(CMD_RESTART_NOW);
    }
}

void applet_display_main(void *arg)
{
    window_t *win = wm_create("Display", DSP_W, DSP_H, WF_NO_MIN | WF_NO_MAX, &display_def);
    if (!win) return;
    wm_center(win);
    res_t res[32];
    int nres = build_resolutions(res, 32);
    int sel = 0, bpp = gfx_cur_bpp(), top = 0, tab = 0, pressed = -1, drag = 0;
    for (int i = 0; i < nres; i++) if (res[i].w == SCREEN_WIDTH && res[i].h == SCREEN_HEIGHT) sel = i;
    if (sel >= 7) top = sel - 6;
    dsp_paint(win, res, nres, sel, top, bpp, tab, pressed, wm_get_scheme());

    event_t ev;
    for (;;) {
        wm_next_event(win, &ev, -1);
        int maxtop = nres > 7 ? nres - 7 : 0;
        switch (ev.type) {
        case EV_CLOSE: wm_destroy(win); return;
        case EV_KEY: if (ev.a == 27) { wm_destroy(win); return; } break;
        case EV_MOUSE_DOWN:
            if (ev.c != 1) break;
            pressed = -1;
            if (ev.b >= 6 && ev.b < 28) { if (ev.a >= 8 && ev.a < 100) tab = 0; else if (ev.a >= 100 && ev.a < 200) tab = 1; }
            else if (ui_inside(ev.a, ev.b, DSP_W - 184, 276, 84, 28) && tab == 0) pressed = 0;
            else if (ui_inside(ev.a, ev.b, DSP_W - 92, 276, 84, 28)) pressed = 1;
            else if (tab == 0) {
                int np = ui_vscroll_click(16 + 176 - UI_SCROLL_W - 2, 62, 144, nres, 7, top, ev.a, ev.b);
                if (np >= 0) { top = np; drag = 1; }
                else if (ui_inside(ev.a, ev.b, 18, 63, 176 - 4 - UI_SCROLL_W, 140)) {
                    int idx = top + (ev.b - 63) / 20;
                    if (idx < nres) {
                        sel = idx;
                        if (!mode_exists(res[sel].w, res[sel].h, bpp)) {
                            for (int i = 0; i < 5; i++) if (mode_exists(res[sel].w, res[sel].h, depth_list[i])) { bpp = depth_list[i]; break; }
                        }
                    }
                } else {
                    int y = 62;
                    for (int i = 0; i < 7; i++)
                        if (mode_exists(res[sel].w, res[sel].h, depth_list[i]) || depth_list[i] == bpp) {
                            if (ui_inside(ev.a, ev.b, 214, y - 2, 200, 18)) bpp = depth_list[i];
                            y += 24;
                        }
                }
            } else {
                for (int i = 0; i < wm_scheme_count(); i++)
                    if (ui_inside(ev.a, ev.b, 16, 62 + i * 30, DSP_W - 40, 28)) {
                        wm_set_scheme(i);
                        g_settings.scheme = (u8)i;
                        settings_save();
                    }
            }
            break;
        case EV_MOUSE_MOVE:
            if (drag) {
                int np = ui_vscroll_click(16 + 176 - UI_SCROLL_W - 2, 62, 144, nres, 7, top, 16 + 176 - UI_SCROLL_W + 2, ev.b);
                if (np >= 0) top = np;
            }
            break;
        case EV_MOUSE_UP:
            drag = 0;
            if (pressed == 0 && ui_inside(ev.a, ev.b, DSP_W - 184, 276, 84, 28)) {
                pressed = -1;
                dsp_paint(win, res, nres, sel, top, bpp, tab, pressed, wm_get_scheme());
                dsp_apply(res[sel].w, res[sel].h, bpp);
            } else if (pressed == 1 && ui_inside(ev.a, ev.b, DSP_W - 92, 276, 84, 28)) {
                wm_destroy(win);
                return;
            }
            pressed = -1;
            break;
        default: break;
        }
        if (top > maxtop) top = maxtop;
        if (top < 0) top = 0;
        dsp_paint(win, res, nres, sel, top, bpp, tab, pressed, wm_get_scheme());
    }
}

/* ---------------------------------------------------------------- Input applet */

const app_def_t input_def = { "Input", ICON_POINTER, { 0, 0 }, { 0, 0 }, { 0, 0 } };

#define IN_W 392
#define IN_H 330

static void in_paint(window_t *win, int test_flash, int pressed)
{
    surface_t *s = wm_surface(win);
    s_fill(s, 0, 0, IN_W, IN_H, COL_LIGHT_GREY);
    ui_groupbox(s, 10, 8, IN_W - 20, 94, "Pointing device");
    static const char *names[3] = { "Mouse", "Keyboard navigation (arrows move, Ctrl clicks)", "Touch (hide the pointer)" };
    for (int i = 0; i < 3; i++) ui_radio(s, 24, 30 + i * 22, i == 1 ? "Keyboard navigation" : names[i], (int)input_mode_get() == i);
    ui_groupbox(s, 10, 112, IN_W - 20, 172, "Mouse");
    s_text(s, 24, 134, "Pointer speed", COL_BLACK, COL_NONE);
    ui_slider(s, 24, 154, 200, 50, 300, mouse_get_speed());
    char b[24];
    ksnprintf(b, sizeof(b), "%d%%", mouse_get_speed());
    s_text(s, 236, 152, b, COL_BLACK, COL_NONE);
    s_text(s, 24, 182, "Double-click speed", COL_BLACK, COL_NONE);
    ui_slider(s, 24, 202, 200, 10, 100, 110 - wm_get_dblclick_ticks());
    s_text(s, 24, 222, "slow", COL_DARK_GREY, COL_NONE);
    s_text(s, 200 - 8, 222, "fast", COL_DARK_GREY, COL_NONE);
    s_fill(s, 262, 180, 112, 70, test_flash ? COL_YELLOW : COL_WHITE);
    s_bevel(s, 262, 180, 112, 70, 1);
    ui_text_center(s, 262, 196, 112, "Double-click", COL_BLACK);
    ui_text_center(s, 262, 214, 112, "to test", COL_BLACK);
    ui_button(s, IN_W - 94, IN_H - 40, 84, 28, "Close", pressed, 0);
    wm_invalidate(win);
}

void applet_input_main(void *arg)
{
    window_t *win = wm_create("Input", IN_W, IN_H, WF_NO_MIN | WF_NO_MAX, &input_def);
    if (!win) return;
    wm_center(win);
    int flash = 0, pressed = 0, slider = 0;     /* slider: 1 speed, 2 double-click */
    unsigned long flash_until = 0;
    in_paint(win, flash, pressed);
    event_t ev;
    for (;;) {
        int got = wm_next_event(win, &ev, 20);
        if (flash && timer_ticks > flash_until) { flash = 0; in_paint(win, flash, pressed); }
        if (!got) continue;
        switch (ev.type) {
        case EV_CLOSE: wm_destroy(win); return;
        case EV_KEY: if (ev.a == 27 || ev.a == '\n') { wm_destroy(win); return; } break;
        case EV_MOUSE_DOWN:
            if (ev.c != 1) break;
            for (int i = 0; i < 3; i++)
                if (ui_inside(ev.a, ev.b, 24, 28 + i * 22, 300, 18)) input_mode_set((input_mode_t)i);
            if (ui_inside(ev.a, ev.b, 24, 150, 210, 22)) { slider = 1; mouse_set_speed(ui_slider_value(24, 200, 50, 300, ev.a)); }
            if (ui_inside(ev.a, ev.b, 24, 198, 210, 22)) { slider = 2; wm_set_dblclick_ticks(110 - ui_slider_value(24, 200, 10, 100, ev.a)); }
            if (ui_inside(ev.a, ev.b, 262, 180, 112, 70) && ev.d) { flash = 1; flash_until = timer_ticks + 60; }
            if (ui_inside(ev.a, ev.b, IN_W - 94, IN_H - 40, 84, 28)) pressed = 1;
            break;
        case EV_MOUSE_MOVE:
            if (slider == 1) mouse_set_speed(ui_slider_value(24, 200, 50, 300, ev.a));
            if (slider == 2) wm_set_dblclick_ticks(110 - ui_slider_value(24, 200, 10, 100, ev.a));
            break;
        case EV_MOUSE_UP:
            if (slider) {
                g_settings.mouse_pct = (u16)mouse_get_speed();
                g_settings.dbl_ticks = (u8)wm_get_dblclick_ticks();
                settings_save();
                slider = 0;
            }
            if (pressed && ui_inside(ev.a, ev.b, IN_W - 94, IN_H - 40, 84, 28)) { wm_destroy(win); return; }
            pressed = 0;
            break;
        default: break;
        }
        in_paint(win, flash, pressed);
    }
}

/* ---------------------------------------------------------------- Sound applet */

const app_def_t sound_def = { "Sound", ICON_DISC, { 0, 0 }, { 0, 0 }, { 0, 0 } };

#define SN_W 360
#define SN_H 220

static void test_sound_task(void *arg) { audio_play_file("CHIME.WAV", 0, 0); }

static void sn_paint(window_t *win, int pressed_btn)
{
    surface_t *s = wm_surface(win);
    s_fill(s, 0, 0, SN_W, SN_H + 30, COL_LIGHT_GREY);
    icon_draw(s, 14, 12, 48, ICON_DISC);
    if (sb16_present()) {
        s_text(s, 76, 18, "Sound Blaster 16", COL_BLACK, COL_NONE);
        s_text(s, 76, 38, "Detected and ready.", COL_DARK_GREY, COL_NONE);
    } else {
        s_text(s, 76, 18, "No sound card detected.", COL_BLACK, COL_NONE);
        s_text(s, 76, 38, "Start QEMU with -device sb16.", COL_DARK_GREY, COL_NONE);
    }
    ui_groupbox(s, 10, 70, SN_W - 20, 64, "Volume");
    int dis = !sb16_present();
    ui_slider(s, 24, 96, 240, 0, 100, sb16_get_volume());
    char b[16];
    ksnprintf(b, sizeof(b), "%d%%", sb16_get_volume());
    s_text(s, 276, 94, b, dis ? COL_DARK_GREY : COL_BLACK, COL_NONE);

    ui_groupbox(s, 10, 142, SN_W - 20, 52, "Startup Audio");
    ui_checkbox(s, 24, 162, "Play boot chime sound", g_settings.boot_chime, dis);

    ui_button(s, 10, SN_H + 30 - 40, 110, 28, "Test Sound", pressed_btn == 0, dis);
    ui_button(s, SN_W - 94, SN_H + 30 - 40, 84, 28, "Close", pressed_btn == 1, 0);
    wm_invalidate(win);
}

void applet_sound_main(void *arg)
{
    window_t *win = wm_create("Sound", SN_W, SN_H + 30, WF_NO_MIN | WF_NO_MAX, &sound_def);
    if (!win) return;
    wm_center(win);
    int pressed = -1, slider = 0;
    sn_paint(win, pressed);
    event_t ev;
    for (;;) {
        wm_next_event(win, &ev, -1);
        switch (ev.type) {
        case EV_CLOSE: wm_destroy(win); return;
        case EV_KEY: if (ev.a == 27 || ev.a == '\n') { wm_destroy(win); return; } break;
        case EV_MOUSE_DOWN:
            if (ev.c != 1) break;
            if (ui_inside(ev.a, ev.b, 24, 92, 250, 22)) { slider = 1; sb16_set_volume(ui_slider_value(24, 240, 0, 100, ev.a)); }
            if (ui_inside(ev.a, ev.b, 24, 162, 200, 20)) {
                g_settings.boot_chime = !g_settings.boot_chime;
                settings_save();
            }
            if (ui_inside(ev.a, ev.b, 10, SN_H + 30 - 40, 110, 28) && sb16_present()) pressed = 0;
            if (ui_inside(ev.a, ev.b, SN_W - 94, SN_H + 30 - 40, 84, 28)) pressed = 1;
            break;
        case EV_MOUSE_MOVE:
            if (slider) sb16_set_volume(ui_slider_value(24, 240, 0, 100, ev.a));
            break;
        case EV_MOUSE_UP:
            if (slider) { g_settings.volume = (u8)sb16_get_volume(); settings_save(); slider = 0; }
            if (pressed == 0 && ui_inside(ev.a, ev.b, 10, SN_H + 30 - 40, 110, 28)) task_create("sound test", test_sound_task, 0, 0);
            if (pressed == 1 && ui_inside(ev.a, ev.b, SN_W - 94, SN_H + 30 - 40, 84, 28)) { wm_destroy(win); return; }
            pressed = -1;
            break;
        default: break;
        }
        sn_paint(win, pressed);
    }
}
