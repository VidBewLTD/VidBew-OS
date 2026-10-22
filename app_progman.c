/* app_progman.c -- Program Manager: the launcher window of big icons. */

#include "apps.h"
#include "ui.h"
#include "keyboard.h"
#include "kstd.h"
#include "audio.h"
#include "sb16.h"
#include "timer.h"

#define CELL_W 92
#define CELL_H 86
#define ICON_SIZE 48

typedef struct { const char *label; icon_type_t icon; int action; } pm_item_t;

enum { ACT_MODE = -1, ACT_SOUND = -2 };

static const pm_item_t items[] = {
    { "Explorer",   ICON_FOLDER,   APP_EXPLORER },
    { "Terminal",   ICON_MONITOR,  APP_TERMINAL },
    { "Control",    ICON_CONTROL,  APP_CONTROL },
    { "Tasks",      ICON_TASKS,    APP_TASKMAN },
    { "Clock",      ICON_CLOCK,    APP_CLOCK },
    { "Calc",       ICON_CONTROL,  APP_CALC },
    { "Calendar",   ICON_CALENDAR, APP_CALENDAR },
    { "Alarm",      ICON_CLOCK,    APP_ALARM },
    { "Paint",      ICON_DISC,     APP_PAINT },
    { "Images",     ICON_DISPLAY,  APP_VIEWER },
    { "Keyboard",   ICON_KEYBOARD, APP_OSK },
    { "Mode",       ICON_POINTER,  ACT_MODE },
    { "Sound",      ICON_DISC,     ACT_SOUND },
    { "Network",    ICON_NETWORK,  APP_NETWORK },
    { "Help",       ICON_HELP,     APP_HELP },
    { "About",      ICON_INFO,     APP_ABOUT },
};
#define NITEMS ((int)(sizeof(items) / sizeof(items[0])))

static const menu_item_t file_items[] = {
    { "Open",            1, 0, 0 },
};
const app_def_t progman_def = { "Program Manager", ICON_LOGO, { 1, file_items }, { 0, 0 }, { 0, 0 } };

static void paint(window_t *win, int sel)
{
    surface_t *s = wm_surface(win);
    int cw, ch;
    wm_client_size(win, &cw, &ch);
    if (s->w < cw || s->h < ch) return;
    s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
    int cols = cw / CELL_W;
    if (cols < 1) cols = 1;
    for (int i = 0; i < NITEMS; i++) {
        int x = (i % cols) * CELL_W, y = (i / cols) * CELL_H;
        icon_draw(s, x + (CELL_W - ICON_SIZE) / 2, y + 6, ICON_SIZE, items[i].icon);
        int tw = s_text_width(items[i].label);
        int lx = x + (CELL_W - tw) / 2, ly = y + ICON_SIZE + 12;
        if (i == sel) {
            s_fill(s, lx - 2, ly - 1, tw + 4, 18, COL_BLUE);
            s_text(s, lx, ly, items[i].label, COL_WHITE, COL_NONE);
        } else {
            s_text(s, lx, ly, items[i].label, COL_BLACK, COL_NONE);
        }
    }
    wm_invalidate(win);
}

static void run_item(int i)
{
    int a = items[i].action;
    if (a == ACT_MODE) wm_post_command(CMD_CYCLE_MODE);
    else if (a == ACT_SOUND) audio_play_file("CHIME.WAV", 0, 0);
    else app_launch(a);
}

void app_progman_main(void *arg)
{
    window_t *win = wm_create("Program Manager", 5 * CELL_W, 3 * CELL_H + 4, WF_RESIZABLE, &progman_def);
    if (!win) return;
    {   /* dock it along the bottom of the work area, right of the desktop icons */
        int ow, oh;
        wm_outer_size(win, &ow, &oh);
        wm_set_pos(win, 130, SCREEN_HEIGHT - TASKBAR_H - oh - 8);
    }
    wm_set_min_size(win, CELL_W + 4, CELL_H);
    int sel = -1;
    paint(win, sel);

    event_t ev;
    for (;;) {
        wm_next_event(win, &ev, -1);
        switch (ev.type) {
        case EV_CLOSE: wm_destroy(win); return;
        case EV_RESIZE:
            wm_resize_client(win, ev.a, ev.b);
            paint(win, sel);
            break;
        case EV_REPAINT: case EV_FOCUS: case EV_BLUR: paint(win, sel); break;
        case EV_MOUSE_DOWN:
            if (ev.c == 1) {
                int cw, ch;
                wm_client_size(win, &cw, &ch);
                int cols = cw / CELL_W;
                if (cols < 1) cols = 1;
                int col = ev.a / CELL_W, row = ev.b / CELL_H;
                int idx = row * cols + col;
                if (col < cols && idx >= 0 && idx < NITEMS) {
                    sel = idx;
                    paint(win, sel);
                    if (ev.d) run_item(idx);
                } else {
                    sel = -1;
                    paint(win, sel);
                }
            }
            break;
        case EV_MENU:
            if (ev.a == 1 && sel >= 0) run_item(sel);
            break;
        case EV_KEY:
            if (ev.a == '\n' && sel >= 0) run_item(sel);
            break;
        default: break;
        }
    }
}
