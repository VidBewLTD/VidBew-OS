/* app_misc.c -- Clock, About, Help and Task Manager. */

#include "apps.h"
#include "ui.h"
#include "kstd.h"
#include "task.h"
#include "heap.h"
#include "rtc.h"
#include "timer.h"
#include "keyboard.h"

/* ---------------------------------------------------------------- Clock */

const app_def_t clock_def = { "Clock", ICON_CLOCK, { 0, 0 }, { 0, 0 }, { 0, 0 } };

/* sine in 1/1000ths for 0..59 minute marks, computed once with a small table */
static const int sin60[60] = {
    0,105,208,309,407,500,588,669,743,809,866,914,951,978,995,
    1000,995,978,951,914,866,809,743,669,588,500,407,309,208,105,
    0,-105,-208,-309,-407,-500,-588,-669,-743,-809,-866,-914,-951,-978,-995,
    -1000,-995,-978,-951,-914,-866,-809,-743,-669,-588,-500,-407,-309,-208,-105
};

static int sin_m(int m) { return sin60[((m % 60) + 60) % 60]; }
static int cos_m(int m) { return sin60[(((m + 15) % 60) + 60) % 60]; }

static void hand(surface_t *s, int cx, int cy, int len, int minute60, int color, int thick)
{
    int x = cx + sin_m(minute60) * len / 1000;
    int y = cy - cos_m(minute60) * len / 1000;
    s_line(s, cx, cy, x, y, color);
    if (thick) {
        s_line(s, cx + 1, cy, x + 1, y, color);
        s_line(s, cx, cy + 1, x, y + 1, color);
    }
}

static void clock_paint(window_t *win)
{
    surface_t *s = wm_surface(win);
    int cw, ch;
    wm_client_size(win, &cw, &ch);
    if (s->w < cw || s->h < ch) return;
    int h, m, sec;
    rtc_read_time(&h, &m, &sec);
    s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
    int face_h = ch - 24;
    int r = (cw < face_h ? cw : face_h) / 2 - 6;
    if (r < 20) r = 20;
    int cx = cw / 2, cy = face_h / 2 + 2;
    s_fcircle(s, cx, cy, r, COL_WHITE);
    s_circle(s, cx, cy, r, COL_BLACK);
    s_circle(s, cx, cy, r - 1, COL_DARK_GREY);
    for (int i = 0; i < 60; i += 5) {
        int x0 = cx + sin_m(i) * (r - 8) / 1000, y0 = cy - cos_m(i) * (r - 8) / 1000;
        int x1 = cx + sin_m(i) * (r - 3) / 1000, y1 = cy - cos_m(i) * (r - 3) / 1000;
        s_line(s, x0, y0, x1, y1, COL_BLACK);
    }
    hand(s, cx, cy, r * 5 / 10, (h % 12) * 5 + m / 12, COL_BLACK, 1);
    hand(s, cx, cy, r * 8 / 10, m, COL_BLUE, 1);
    hand(s, cx, cy, r * 9 / 10, sec, COL_LIGHT_RED, 0);
    s_fcircle(s, cx, cy, 3, COL_BLACK);
    char t[16];
    ksnprintf(t, sizeof(t), "%02d:%02d:%02d", h, m, sec);
    ui_text_center(s, 0, ch - 20, cw, t, COL_BLACK);
    wm_invalidate(win);
}

void app_clock_main(void *arg)
{
    window_t *win = wm_create("Clock", 200, 224, WF_RESIZABLE, &clock_def);
    if (!win) return;
    wm_set_min_size(win, 120, 150);
    clock_paint(win);
    event_t ev;
    for (;;) {
        if (wm_next_event(win, &ev, 50)) {
            if (ev.type == EV_CLOSE) { wm_destroy(win); return; }
            if (ev.type == EV_RESIZE) wm_resize_client(win, ev.a, ev.b);
        }
        clock_paint(win);        /* once a second at most is plenty; the wait above paces it */
    }
}

/* ---------------------------------------------------------------- About */

const app_def_t about_def = { "About VidBew/OS", ICON_INFO, { 0, 0 }, { 0, 0 }, { 0, 0 } };

void app_about_main(void *arg)
{
    window_t *win = wm_create("About VidBew/OS", 380, 250, WF_NO_MIN, &about_def);
    if (!win) return;
    wm_center(win);
    surface_t *s = wm_surface(win);
    int pressed = 0;
    for (;;) {
        s_fill(s, 0, 0, 380, 250, COL_LIGHT_GREY);
        icon_draw(s, 20, 18, 64, ICON_LOGO);
        s_text(s, 104, 20, "VidBew/OS 1.0", COL_BLACK, COL_NONE);
        s_text(s, 104, 40, "Nova desktop - Heroine kernel", COL_BLACK, COL_NONE);
        s_hline(s, 16, 96, 348, COL_DARK_GREY);
        s_hline(s, 16, 97, 348, COL_WHITE);
        s_text(s, 20, 108, "Written from scratch in C and x86 assembly.", COL_BLACK, COL_NONE);
        s_text(s, 20, 126, "Preemptive multitasking, a window manager,", COL_BLACK, COL_NONE);
        s_text(s, 20, 144, "FAT16, networking and selectable displays.", COL_BLACK, COL_NONE);
        unsigned used, freeb, total;
        heap_stats(&used, &freeb, &total);
        char m[64];
        ksnprintf(m, sizeof(m), "Memory in use: %u KB of %u KB", used / 1024, total / 1024);
        s_text(s, 20, 172, m, COL_DARK_GREY, COL_NONE);
        ui_button(s, 148, 204, 84, 26, "OK", pressed, 0);
        wm_invalidate(win);

        event_t ev;
        wm_next_event(win, &ev, -1);
        if (ev.type == EV_CLOSE || (ev.type == EV_KEY && (ev.a == '\n' || ev.a == 27))) break;
        if (ev.type == EV_MOUSE_DOWN && ui_inside(ev.a, ev.b, 148, 204, 84, 26)) pressed = 1;
        if (ev.type == EV_MOUSE_UP) {
            if (pressed && ui_inside(ev.a, ev.b, 148, 204, 84, 26)) break;
            pressed = 0;
        }
    }
    wm_destroy(win);
}

/* ---------------------------------------------------------------- Help */

const app_def_t help_def = { "Help", ICON_HELP, { 0, 0 }, { 0, 0 }, { 0, 0 } };

static const char *help_lines[] = {
    "VidBew/OS quick guide",
    "",
    "Windows",
    "  Drag a title bar to move a window; drag the lower-right",
    "  corner to resize. Double-click a title bar to maximize.",
    "  Every window is its own task, so they all keep running",
    "  while you use another one.",
    "",
    "Menu bar (top)",
    "  VidBew, File, Edit, View, Special and Help. File, Edit and",
    "  View change to match the window you are working in.",
    "",
    "Taskbar (bottom)",
    "  Start menu, one button per window (click to restore or",
    "  minimize), and the network / input-mode tray.",
    "",
    "Keys",
    "  Alt+Tab     switch windows       Alt+F4   close window",
    "  F1          on-screen keyboard   F2       cycle input mode",
    "  Alt+Arrows  move the pointer with the keyboard",
    "  Left Ctrl   click (in KeyNav and Touch modes)",
    "",
    "Control Panel",
    "  Display (resolution, colors, color scheme), Network (DHCP",
    "  or a fixed address), Input, Sound, Date & Time and System.",
    "",
    "Terminal (Heroine)",
    "  Type 'guide' for the full list. Network commands:",
    "  tether (status), summon (DHCP), beacon <host> (ping),",
    "  seek <name> (DNS), anchor <ip> <mask> <gw> (static).",
    "  Tab completes commands; Up/Down recall history.",
};
#define HELP_N ((int)(sizeof(help_lines) / sizeof(help_lines[0])))

static void help_paint(window_t *win, int top)
{
    surface_t *s = wm_surface(win);
    int cw, ch;
    wm_client_size(win, &cw, &ch);
    if (s->w < cw || s->h < ch) return;
    int rows = (ch - 8) / 18;
    s_fill(s, 0, 0, cw, ch, COL_WHITE);
    for (int i = 0; i < rows && top + i < HELP_N; i++) {
        const char *l = help_lines[top + i];
        int head = (top + i == 0) || (l[0] && l[0] != ' ');
        s_text(s, 8, 6 + i * 18, l, head ? COL_BLUE : COL_BLACK, COL_NONE);
    }
    ui_vscroll(s, cw - UI_SCROLL_W, 0, ch, HELP_N, rows, top);
    wm_invalidate(win);
}

void app_help_main(void *arg)
{
    window_t *win = wm_create("VidBew/OS Help", 520, 360, WF_RESIZABLE, &help_def);
    if (!win) return;
    wm_set_min_size(win, 240, 120);
    int top = 0;
    help_paint(win, top);
    event_t ev;
    for (;;) {
        wm_next_event(win, &ev, -1);
        int cw, ch;
        wm_client_size(win, &cw, &ch);
        int rows = (ch - 8) / 18, maxtop = HELP_N > rows ? HELP_N - rows : 0;
        switch (ev.type) {
        case EV_CLOSE: wm_destroy(win); return;
        case EV_RESIZE: wm_resize_client(win, ev.a, ev.b); break;
        case EV_KEY:
            if (ev.a == KEY_DOWN || ev.a == KEY_PGDN) top += ev.a == KEY_PGDN ? rows : 1;
            if (ev.a == KEY_UP || ev.a == KEY_PGUP) top -= ev.a == KEY_PGUP ? rows : 1;
            if (ev.a == KEY_HOME) top = 0;
            if (ev.a == KEY_END) top = maxtop;
            break;
        case EV_MOUSE_DOWN: case EV_MOUSE_MOVE: {
            int np = ui_vscroll_click(cw - UI_SCROLL_W, 0, ch, HELP_N, rows, top, ev.a, ev.b);
            if (np >= 0) top = np;
            break;
        }
        default: break;
        }
        if (top < 0) top = 0;
        if (top > maxtop) top = maxtop;
        help_paint(win, top);
    }
}

/* ---------------------------------------------------------------- Task Manager */

static const menu_item_t tm_file[] = {
    { "End Task",   1, 0, 0 },
    { "Switch To",  2, 0, 0 },
};
static const menu_item_t tm_view[] = {
    { "Refresh Now", 3, 0, 0 },
};
const app_def_t taskman_def = { "Task Manager", ICON_TASKS, { 2, tm_file }, { 0, 0 }, { 1, tm_view } };

typedef struct { int id; char name[16]; int state; int cpu; } trow_t;

#define TM_ROW_H 18
#define TM_HEAD 24

static int tm_collect(trow_t *rows, int max, unsigned long *prev_ticks, unsigned long *prev_total, unsigned long *prev_idle, int *idle_pct)
{
    unsigned long total = task_total_ticks(), idle = task_idle_ticks();
    unsigned long dt = total - *prev_total;
    int n = 0;
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        int st = task_state(i);
        if (st == TASK_FREE) continue;
        rows[n].id = i;
        kstrlcpy(rows[n].name, task_name(i), sizeof(rows[n].name));
        rows[n].state = st;
        unsigned long t = task_cpu_ticks(i);
        rows[n].cpu = dt ? (int)((t - prev_ticks[i]) * 100 / dt) : 0;
        prev_ticks[i] = t;
        n++;
    }
    *idle_pct = dt ? (int)((idle - *prev_idle) * 100 / dt) : 100;
    *prev_total = total;
    *prev_idle = idle;
    return n;
}

static void tm_paint(window_t *win, trow_t *rows, int n, int sel, int idle_pct, int top)
{
    surface_t *s = wm_surface(win);
    int cw, ch;
    wm_client_size(win, &cw, &ch);
    if (s->w < cw || s->h < ch) return;
    s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
    int list_h = ch - 96;
    s_fill(s, 6, 6, cw - 12, list_h, COL_WHITE);
    s_bevel(s, 6, 6, cw - 12, list_h, 1);
    s_fill(s, 8, 8, cw - 16, TM_HEAD - 6, COL_LIGHT_GREY);
    s_text(s, 12, 8, "ID", COL_BLACK, COL_NONE);
    s_text(s, 44, 8, "Task", COL_BLACK, COL_NONE);
    s_text(s, 180, 8, "State", COL_BLACK, COL_NONE);
    s_text(s, cw - 100, 8, "CPU %", COL_BLACK, COL_NONE);
    int rows_vis = (list_h - TM_HEAD - 4) / TM_ROW_H;
    for (int i = 0; i < rows_vis && top + i < n; i++) {
        trow_t *r = &rows[top + i];
        int y = 8 + TM_HEAD + i * TM_ROW_H;
        int hot = (top + i == sel);
        if (hot) s_fill(s, 8, y, cw - 16 - UI_SCROLL_W, TM_ROW_H, COL_BLUE);
        int fg = hot ? COL_WHITE : COL_BLACK;
        char b[16];
        ksnprintf(b, sizeof(b), "%d", r->id);
        s_text(s, 12, y + 1, b, fg, COL_NONE);
        s_text(s, 44, y + 1, r->name, fg, COL_NONE);
        s_text(s, 180, y + 1, r->state == TASK_SLEEPING ? "Waiting" : "Running", fg, COL_NONE);
        ksnprintf(b, sizeof(b), "%d", r->cpu);
        s_text(s, cw - 100, y + 1, b, fg, COL_NONE);
    }
    ui_vscroll(s, cw - 6 - UI_SCROLL_W - 2, 8 + TM_HEAD, list_h - TM_HEAD - 4, n, rows_vis, top);

    unsigned used, freeb, total;
    heap_stats(&used, &freeb, &total);
    int by = list_h + 12;
    char m[96];
    ksnprintf(m, sizeof(m), "CPU in use: %d%%    Tasks: %d    Windows: %d", 100 - idle_pct, n, wm_window_count());
    s_text(s, 8, by, m, COL_BLACK, COL_NONE);
    ui_progress(s, 8, by + 20, cw - 16, 14, 100 - idle_pct);
    ksnprintf(m, sizeof(m), "Memory: %u KB used of %u KB", used / 1024, total / 1024);
    s_text(s, 8, by + 38, m, COL_BLACK, COL_NONE);
    ui_button(s, cw - 96, ch - 30, 88, 24, "End Task", 0, sel < 1);
    wm_invalidate(win);
}

void app_taskman_main(void *arg)
{
    window_t *win = wm_create("Task Manager", 400, 340, WF_RESIZABLE, &taskman_def);
    if (!win) return;
    wm_set_min_size(win, 320, 220);
    static unsigned long prev_ticks[MAX_TASKS];
    unsigned long prev_total = task_total_ticks(), prev_idle = task_idle_ticks();
    trow_t rows[MAX_TASKS];
    int n = 0, sel = -1, idle_pct = 100, top = 0;
    for (int i = 0; i < MAX_TASKS; i++) prev_ticks[i] = task_cpu_ticks(i);
    n = tm_collect(rows, MAX_TASKS, prev_ticks, &prev_total, &prev_idle, &idle_pct);
    tm_paint(win, rows, n, sel, idle_pct, top);

    unsigned long last = timer_ticks;
    event_t ev;
    for (;;) {
        int got = wm_next_event(win, &ev, 25);
        int cw, ch;
        wm_client_size(win, &cw, &ch);
        int list_h = ch - 96, rows_vis = (list_h - TM_HEAD - 4) / TM_ROW_H;
        if (got) {
            if (ev.type == EV_CLOSE) { wm_destroy(win); return; }
            if (ev.type == EV_RESIZE) wm_resize_client(win, ev.a, ev.b);
            if (ev.type == EV_MOUSE_DOWN && ev.c == 1) {
                if (ui_inside(ev.a, ev.b, cw - 96, ch - 30, 88, 24)) ev.type = EV_MENU, ev.a = 1;
                else {
                    int np = ui_vscroll_click(cw - 6 - UI_SCROLL_W - 2, 8 + TM_HEAD, list_h - TM_HEAD - 4, n, rows_vis, top, ev.a, ev.b);
                    if (np >= 0) top = np;
                    else if (ev.b >= 8 + TM_HEAD && ev.b < 8 + TM_HEAD + rows_vis * TM_ROW_H) {
                        int idx = top + (ev.b - 8 - TM_HEAD) / TM_ROW_H;
                        sel = idx < n ? idx : -1;
                    }
                }
            }
            if (ev.type == EV_MENU) {
                if (ev.a == 1 && sel >= 0 && sel < n && rows[sel].id > 0) {
                    int id = rows[sel].id;
                    if (id != task_current()) wm_close_by_owner(id);
                    sel = -1;
                }
            }
        }
        if (timer_ticks - last >= 50 || got) {
            if (timer_ticks - last >= 50) {
                last = timer_ticks;
                int old_id = (sel >= 0 && sel < n) ? rows[sel].id : -1;
                n = tm_collect(rows, MAX_TASKS, prev_ticks, &prev_total, &prev_idle, &idle_pct);
                sel = -1;
                for (int i = 0; i < n; i++) if (rows[i].id == old_id) sel = i;
            }
            int maxtop = n > rows_vis ? n - rows_vis : 0;
            if (top > maxtop) top = maxtop;
            tm_paint(win, rows, n, sel, idle_pct, top);
        }
    }
}
