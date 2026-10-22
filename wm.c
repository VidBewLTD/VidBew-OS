/* wm.c -- the Nova window manager: windows, compositor, menu bar, taskbar,
 * Start menu, desktop icons and all pointer/keyboard routing.
 *
 * Threading model: app tasks draw only into their own window's client
 * surface and post/consume events through per-window queues. The UI task
 * (kernel main loop) calls wm_ui_iteration() on every wake-up: it drains
 * input, turns app invalidations into screen damage, composes the damaged
 * rectangles into the backbuffer and presents them. wm_lock serialises
 * everything that changes the window list. */

#include "wm.h"
#include "apps.h"
#include "ui.h"
#include "mouse.h"
#include "keyboard.h"
#include "input_mode.h"
#include "timer.h"
#include "task.h"
#include "heap.h"
#include "kstd.h"
#include "sync.h"
#include "rtc.h"
#include "net.h"
#include "serial.h"

#define MAX_WIN   24
#define EVQ       32
#define TITLE_H   18
#define FRAME     3
#define CLIENT_DY (FRAME + TITLE_H + 1)
#define BTN_W     16
#define BTN_H     14
#define WM_AUTO   (-30000)

#define WS_NORMAL 0
#define WS_MIN    1
#define WS_MAX    2

struct window {
    int used, id;
    char title[48];
    int x, y, w, h;               /* outer rectangle on screen */
    int cw, ch;                   /* logical client size */
    int min_cw, min_ch;
    unsigned flags;
    int state;
    int rx, ry, rcw, rch;         /* geometry to restore after maximize */
    surface_t client;             /* may lag cw/ch until the app handles EV_RESIZE */
    int owner;
    const app_def_t *app;
    event_t evq[EVQ];
    volatile int eh, et;
    volatile int dirty;
    volatile int dx0, dy0, dx1, dy1;
    unsigned long close_req;
};

typedef struct { int x0, y0, x1, y1; } rect_t;

static window_t wins[MAX_WIN];
static window_t *zorder[MAX_WIN];  static int zcount = 0;      /* back -> front */
static window_t *taskorder[MAX_WIN]; static int tcount = 0;    /* creation order, for the taskbar */
static window_t *focus_win = 0;
static int next_id = 1;
static int cascade_n = 0;
static mutex_t wm_lock;

/* ---------------------------------------------------------------- schemes */

#define CUBE(r, g, b) (16 + 36 * (r) + 6 * (g) + (b))

typedef struct {
    const char *name;
    u8 desktop, title_a, title_a_text, title_i, title_i_text, menubar, menubar_text;
} scheme_t;

static const scheme_t schemes[] = {
    { "VidBew Classic", COL_CYAN,     COL_BLUE,   COL_WHITE, COL_DARK_GREY, COL_LIGHT_GREY, COL_BLUE,   COL_WHITE },
    { "Deep Sea",       CUBE(0,2,3),  CUBE(0,1,3), COL_WHITE, COL_DARK_GREY, COL_LIGHT_GREY, CUBE(0,1,3), COL_WHITE },
    { "Rose Garden",    CUBE(3,1,2),  CUBE(3,0,1), COL_WHITE, COL_DARK_GREY, COL_LIGHT_GREY, CUBE(3,0,1), COL_WHITE },
    { "Forest",         CUBE(1,2,1),  CUBE(0,2,0), COL_WHITE, COL_DARK_GREY, COL_LIGHT_GREY, CUBE(0,2,0), COL_WHITE },
    { "Desert",         CUBE(4,3,1),  CUBE(3,1,0), COL_WHITE, COL_DARK_GREY, COL_LIGHT_GREY, CUBE(3,1,0), COL_WHITE },
    { "High Contrast",  COL_BLACK,    COL_YELLOW, COL_BLACK, COL_DARK_GREY, COL_WHITE,      COL_BLACK,  COL_YELLOW },
};
#define SCHEME_COUNT ((int)(sizeof(schemes) / sizeof(schemes[0])))
static int scheme_idx = 0;
static const scheme_t *theme = &schemes[0];

int wm_scheme_count(void) { return SCHEME_COUNT; }
const char *wm_scheme_name(int i) { return (i >= 0 && i < SCHEME_COUNT) ? schemes[i].name : ""; }
int wm_get_scheme(void) { return scheme_idx; }
void wm_scheme_swatch(int i, int *desktop, int *title)
{
    if (i < 0 || i >= SCHEME_COUNT) i = 0;
    *desktop = schemes[i].desktop;
    *title = schemes[i].title_a;
}

/* ---------------------------------------------------------------- clipboard */

static char clip[256];
void clipboard_set(const char *text) { kstrlcpy(clip, text, sizeof(clip)); }
const char *clipboard_get(void) { return clip; }

/* ---------------------------------------------------------------- damage */

#define MAX_DMG 10
static rect_t dmg[MAX_DMG];
static int ndmg = 0;

static int rects_touch(const rect_t *a, const rect_t *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

static void rect_union(rect_t *a, const rect_t *b)
{
    if (b->x0 < a->x0) a->x0 = b->x0;
    if (b->y0 < a->y0) a->y0 = b->y0;
    if (b->x1 > a->x1) a->x1 = b->x1;
    if (b->y1 > a->y1) a->y1 = b->y1;
}

static void damage_add(int x, int y, int w, int h)
{
    rect_t r = { x, y, x + w, y + h };
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;
    if (r.y1 > SCREEN_HEIGHT) r.y1 = SCREEN_HEIGHT;
    if (r.x1 <= r.x0 || r.y1 <= r.y0) return;

    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < ndmg; i++) {
            if (rects_touch(&dmg[i], &r)) {
                rect_union(&r, &dmg[i]);
                dmg[i] = dmg[--ndmg];
                merged = 1;
                break;
            }
        }
    }
    if (ndmg == MAX_DMG) {            /* too fragmented: collapse everything */
        for (int i = 0; i < ndmg; i++) rect_union(&r, &dmg[i]);
        ndmg = 0;
    }
    dmg[ndmg++] = r;
}

static void damage_all(void) { damage_add(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); }
static void damage_window(window_t *w) { damage_add(w->x, w->y, w->w, w->h); }
static void damage_taskbar(void) { damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H); }

/* ---------------------------------------------------------------- geometry */

static void work_area(int *x, int *y, int *w, int *h)
{
    *x = 0;
    *y = MENUBAR_H;
    *w = SCREEN_WIDTH;
    *h = SCREEN_HEIGHT - MENUBAR_H - TASKBAR_H;
}

static void win_update_outer(window_t *w)
{
    w->w = w->cw + 2 * FRAME;
    w->h = w->ch + CLIENT_DY + FRAME;
}

static int rect_hit(const surface_t *s, int x, int y, int w, int h)
{
    return x < s->cx1 && x + w > s->cx0 && y < s->cy1 && y + h > s->cy0;
}

/* ---------------------------------------------------------------- z-order, focus, events */

static void z_remove(window_t *w)
{
    for (int i = 0; i < zcount; i++)
        if (zorder[i] == w) {
            for (int k = i; k < zcount - 1; k++) zorder[k] = zorder[k + 1];
            zcount--;
            return;
        }
}

static void z_insert_top(window_t *w)
{
    int pos = zcount;
    if (!(w->flags & WF_TOPMOST)) {
        for (int i = 0; i < zcount; i++)
            if (zorder[i]->flags & WF_TOPMOST) { pos = i; break; }
    }
    for (int k = zcount; k > pos; k--) zorder[k] = zorder[k - 1];
    zorder[pos] = w;
    zcount++;
}

static void t_remove(window_t *w)
{
    for (int i = 0; i < tcount; i++)
        if (taskorder[i] == w) {
            for (int k = i; k < tcount - 1; k++) taskorder[k] = taskorder[k + 1];
            tcount--;
            return;
        }
}

static void post_event(window_t *w, int type, int a, int b, int c, int d)
{
    u32 f = irq_save();
    int next = (w->eh + 1) % EVQ;
    if (type == EV_MOUSE_MOVE && w->eh != w->et) {
        int last = (w->eh + EVQ - 1) % EVQ;
        if (w->evq[last].type == EV_MOUSE_MOVE) {
            w->evq[last].a = a; w->evq[last].b = b; w->evq[last].c = c;
            irq_restore(f);
            return;
        }
    }
    if (next != w->et) {
        w->evq[w->eh].type = type; w->evq[w->eh].a = a; w->evq[w->eh].b = b;
        w->evq[w->eh].c = c; w->evq[w->eh].d = d;
        w->eh = next;
    } else if (type == EV_CLOSE) {    /* queue full: a close request must still get through */
        int last = (w->eh + EVQ - 1) % EVQ;
        w->evq[last].type = type; w->evq[last].a = a; w->evq[last].b = b;
        w->evq[last].c = c; w->evq[last].d = d;
    }
    irq_restore(f);
    task_wake(w->owner);
}

static window_t *top_focusable(void)
{
    for (int i = zcount - 1; i >= 0; i--) {
        window_t *w = zorder[i];
        if (w->state != WS_MIN && !(w->flags & WF_NO_FOCUS)) return w;
    }
    return 0;
}

static void focus_set(window_t *w)
{
    if (w == focus_win) return;
    window_t *old = focus_win;
    focus_win = w;
    if (old) { post_event(old, EV_BLUR, 0, 0, 0, 0); damage_window(old); }
    if (w) { post_event(w, EV_FOCUS, 0, 0, 0, 0); damage_window(w); }
    damage_taskbar();
}

static void raise_window(window_t *w)
{
    if (zcount && zorder[zcount - 1] == w) return;
    z_remove(w);
    z_insert_top(w);
    damage_window(w);
}

/* ---------------------------------------------------------------- app-facing API */

window_t *wm_create_at(const char *title, int x, int y, int cw, int ch, unsigned flags, const app_def_t *app)
{
    if (cw < 40) cw = 40;
    if (ch < 24) ch = 24;
    u8 *px = (u8 *)kmalloc((unsigned)(cw * ch));
    if (!px) return 0;
    memset(px, COL_LIGHT_GREY, (unsigned)(cw * ch));

    mutex_lock(&wm_lock);
    window_t *w = 0;
    for (int i = 0; i < MAX_WIN; i++) if (!wins[i].used) { w = &wins[i]; break; }
    if (!w) { mutex_unlock(&wm_lock); kfree(px); return 0; }

    memset(w, 0, sizeof(*w));
    w->used = 1;
    w->id = next_id++;
    kstrlcpy(w->title, title, sizeof(w->title));
    w->cw = cw; w->ch = ch;
    w->min_cw = 120; w->min_ch = 60;
    w->flags = flags;
    w->app = app;
    w->owner = task_current();
    surf_init(&w->client, cw, ch, px);
    win_update_outer(w);

    int ax, ay, aw, ah;
    work_area(&ax, &ay, &aw, &ah);
    if (x == WM_AUTO || y == WM_AUTO) {
        x = ax + 130 + (cascade_n % 8) * 26;
        y = ay + 14 + (cascade_n % 8) * 26;
        cascade_n++;
        if (x + w->w > aw) x = aw - w->w;
        if (y + w->h > ay + ah) y = ay + ah - w->h;
        if (x < ax) x = ax;
        if (y < ay) y = ay;
    }
    w->x = x; w->y = y;

    z_insert_top(w);
    taskorder[tcount++] = w;
    if (!(flags & WF_NO_FOCUS)) focus_set(w);
    damage_window(w);
    damage_taskbar();
    mutex_unlock(&wm_lock);
    return w;
}

window_t *wm_create(const char *title, int cw, int ch, unsigned flags, const app_def_t *app)
{
    return wm_create_at(title, WM_AUTO, WM_AUTO, cw, ch, flags, app);
}

void wm_destroy(window_t *w)
{
    if (!w) return;
    mutex_lock(&wm_lock);
    if (!w->used) { mutex_unlock(&wm_lock); return; }
    damage_window(w);
    damage_taskbar();
    z_remove(w);
    t_remove(w);
    u8 *px = w->client.px;
    w->client.px = 0;
    w->used = 0;
    if (w == focus_win) { focus_win = 0; focus_set(top_focusable()); }
    mutex_unlock(&wm_lock);
    kfree(px);
}

void wm_task_exited(int task_id)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].used && wins[i].owner == task_id) wm_destroy(&wins[i]);
}

surface_t *wm_surface(window_t *w) { return &w->client; }

void wm_invalidate_rect(window_t *w, int x, int y, int cw, int ch)
{
    u32 f = irq_save();
    int x1 = x + cw, y1 = y + ch;
    if (!w->dirty) { w->dx0 = x; w->dy0 = y; w->dx1 = x1; w->dy1 = y1; w->dirty = 1; }
    else {
        if (x < w->dx0) w->dx0 = x;
        if (y < w->dy0) w->dy0 = y;
        if (x1 > w->dx1) w->dx1 = x1;
        if (y1 > w->dy1) w->dy1 = y1;
    }
    irq_restore(f);
}

void wm_invalidate(window_t *w) { wm_invalidate_rect(w, 0, 0, w->cw, w->ch); }

int wm_next_event(window_t *w, event_t *ev, int timeout)
{
    unsigned long deadline = timeout >= 0 ? timer_ticks + (unsigned)timeout : 0;
    for (;;) {
        u32 f = irq_save();
        if (w->eh != w->et) {
            *ev = w->evq[w->et];
            w->et = (w->et + 1) % EVQ;
            irq_restore(f);
            return 1;
        }
        unsigned t = 50;
        if (timeout >= 0) {
            long left = (long)(deadline - timer_ticks);
            if (left <= 0) { irq_restore(f); return 0; }
            t = (unsigned)left;
        }
        task_sleep(t);      /* interrupts stay off until the task is asleep: no lost wake-ups */
        irq_restore(f);
    }
}

void wm_set_title(window_t *w, const char *title)
{
    kstrlcpy(w->title, title, sizeof(w->title));
    mutex_lock(&wm_lock);
    damage_add(w->x, w->y, w->w, CLIENT_DY);
    damage_taskbar();
    mutex_unlock(&wm_lock);
}

void wm_set_min_size(window_t *w, int min_cw, int min_ch) { w->min_cw = min_cw; w->min_ch = min_ch; }
void wm_client_size(window_t *w, int *cw, int *ch) { *cw = w->cw; *ch = w->ch; }
void wm_outer_size(window_t *w, int *ow, int *oh) { *ow = w->w; *oh = w->h; }
int  wm_is_focused(window_t *w) { return w == focus_win; }

int wm_resize_client(window_t *w, int cw, int ch)
{
    if (cw < 1 || ch < 1) return 0;
    u8 *px = (u8 *)kmalloc((unsigned)(cw * ch));
    if (!px) return 0;
    memset(px, COL_LIGHT_GREY, (unsigned)(cw * ch));
    mutex_lock(&wm_lock);
    u8 *old = w->client.px;
    surf_init(&w->client, cw, ch, px);
    damage_window(w);
    mutex_unlock(&wm_lock);
    kfree(old);
    return 1;
}

void wm_set_pos(window_t *w, int x, int y)
{
    mutex_lock(&wm_lock);
    damage_window(w);
    w->x = x; w->y = y;
    damage_window(w);
    mutex_unlock(&wm_lock);
}

void wm_center(window_t *w)
{
    int ax, ay, aw, ah;
    work_area(&ax, &ay, &aw, &ah);
    wm_set_pos(w, ax + (aw - w->w) / 2, ay + (ah - w->h) / 2);
}

void wm_dock_bottom(window_t *w)
{
    wm_set_pos(w, (SCREEN_WIDTH - w->w) / 2, SCREEN_HEIGHT - TASKBAR_H - w->h);
}

window_t *wm_find_by_app(const app_def_t *app)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].used && wins[i].app == app) return &wins[i];
    return 0;
}

void wm_activate(window_t *w)
{
    mutex_lock(&wm_lock);
    if (w->state == WS_MIN) { w->state = WS_NORMAL; damage_window(w); }
    raise_window(w);
    if (!(w->flags & WF_NO_FOCUS)) focus_set(w);
    mutex_unlock(&wm_lock);
}

void wm_post_key(int code, int mods)
{
    if (focus_win) post_event(focus_win, EV_KEY, code, mods, 0, 0);
}

int wm_window_count(void) { return tcount; }

int wm_window_info(int index, char *title, int cap, int *owner_task, int *state)
{
    int ok = 0;
    mutex_lock(&wm_lock);
    if (index >= 0 && index < tcount) {
        window_t *w = taskorder[index];
        kstrlcpy(title, w->title, (unsigned)cap);
        *owner_task = w->owner;
        *state = w->state;
        ok = 1;
    }
    mutex_unlock(&wm_lock);
    return ok;
}

/* ---------------------------------------------------------------- commands queue */

#define CMDQ 16
static int cmdq[CMDQ];
static volatile int cq_head = 0, cq_tail = 0;

void wm_post_command(int cmd)
{
    u32 f = irq_save();
    int next = (cq_head + 1) % CMDQ;
    if (next != cq_tail) { cmdq[cq_head] = cmd; cq_head = next; }
    irq_restore(f);
}

static int cmdq_pop(int *cmd)
{
    u32 f = irq_save();
    int ok = 0;
    if (cq_head != cq_tail) { *cmd = cmdq[cq_tail]; cq_tail = (cq_tail + 1) % CMDQ; ok = 1; }
    irq_restore(f);
    return ok;
}

/* ---------------------------------------------------------------- UI state */

typedef struct { const char *label; int cmd; int flags; const char *hint; } mrow_t;

static const char *mb_titles[6] = { "VidBew", "File", "Edit", "View", "Special", "Help" };
static int mb_x[6], mb_w[6];
static int menu_open = -1, menu_hover = -1;
static mrow_t mrows[24];
static int nmrows = 0;
static int start_open = 0, start_hover = -1;
static int desktop_sel = -1;
static char clock_str[12] = "00:00:00";
static int last_sec = -1;

typedef struct { const char *label; icon_type_t icon; int cmd; } dicon_t;
static const dicon_t dicons[] = {
    { "Explorer",      ICON_FOLDER,  CMD_NEW_EXPLORER },
    { "Terminal",      ICON_MONITOR, CMD_NEW_TERMINAL },
    { "Control Panel", ICON_CONTROL, CMD_CONTROL_PANEL },
    { "Task Manager",  ICON_TASKS,   CMD_TASKMAN },
};
#define NDICONS ((int)(sizeof(dicons) / sizeof(dicons[0])))
#define DICON_W    108
#define DICON_H    82
#define DICON_SIZE 48

static const dicon_t sitems[] = {
    { "Program Manager", ICON_LOGO,     CMD_PROGMAN },
    { "File Explorer",   ICON_FOLDER,   CMD_NEW_EXPLORER },
    { "Terminal",        ICON_MONITOR,  CMD_NEW_TERMINAL },
    { "Control Panel",   ICON_CONTROL,  CMD_CONTROL_PANEL },
    { "Task Manager",    ICON_TASKS,    CMD_TASKMAN },
    { "Clock",           ICON_CLOCK,    CMD_CLOCK },
    { "Keyboard",        ICON_KEYBOARD, CMD_OSK },
    { "-",               ICON_FILE,     0 },
    { "Help",            ICON_HELP,     CMD_HELP },
    { "About VidBew/OS", ICON_INFO,     CMD_ABOUT },
    { "-",               ICON_FILE,     0 },
    { "Restart...",      ICON_DISC,     CMD_RESTART },
    { "Shut Down...",    ICON_SYSTEM,   CMD_SHUTDOWN },
};
#define NSITEMS ((int)(sizeof(sitems) / sizeof(sitems[0])))
#define START_W 212

static int is_sep(const char *l) { return l[0] == '-' && l[1] == 0; }
static int mrow_h(const mrow_t *r) { return is_sep(r->label) ? 8 : 20; }
static int sitem_h(int i) { return is_sep(sitems[i].label) ? 8 : 26; }

static void menubar_layout(void)
{
    int x = 4;
    for (int i = 0; i < 6; i++) {
        int w = s_text_width(mb_titles[i]) + 16 + (i == 0 ? 20 : 0);
        mb_x[i] = x;
        mb_w[i] = w;
        x += w;
    }
}

static int clock_x(void) { return SCREEN_WIDTH - 8 - 64; }
static int mode_x(void) { return clock_x() - 20 - s_text_width("KeyNav"); }

/* ---- drop-down menus ---- */

static void build_menu(int idx)
{
    window_t *fw = focus_win;
    const app_def_t *app = fw ? fw->app : 0;
    nmrows = 0;
#define ADD(l, c, f, h) do { mrows[nmrows].label = (l); mrows[nmrows].cmd = (c); mrows[nmrows].flags = (f); mrows[nmrows].hint = (h); nmrows++; } while (0)
#define ADD_DEF(d) for (int k = 0; k < (d).n; k++) ADD((d).items[k].label, (d).items[k].cmd, (d).items[k].flags, (d).items[k].hint)
    switch (idx) {
    case 0:
        ADD("About VidBew/OS...", CMD_ABOUT, 0, 0);
        ADD("-", 0, 0, 0);
        ADD("Control Panel", CMD_CONTROL_PANEL, 0, 0);
        ADD("Task Manager", CMD_TASKMAN, 0, 0);
        ADD("Program Manager", CMD_PROGMAN, 0, 0);
        ADD("Clock", CMD_CLOCK, 0, 0);
        ADD("Calculator", CMD_APP_BASE + APP_CALC, 0, 0);
        ADD("Calendar", CMD_APP_BASE + APP_CALENDAR, 0, 0);
        ADD("Alarm", CMD_APP_BASE + APP_ALARM, 0, 0);
        ADD("Paint", CMD_APP_BASE + APP_PAINT, 0, 0);
        ADD("Image Viewer", CMD_APP_BASE + APP_VIEWER, 0, 0);
        ADD("-", 0, 0, 0);
        ADD("On-Screen Keyboard", CMD_OSK, 0, "F1");
        break;
    case 1:
        if (app && app->file.n) { ADD_DEF(app->file); ADD("-", 0, 0, 0); }
        ADD("New Explorer", CMD_NEW_EXPLORER, 0, 0);
        ADD("New Terminal", CMD_NEW_TERMINAL, 0, 0);
        ADD("-", 0, 0, 0);
        ADD("Close Window", CMD_CLOSE, fw ? 0 : MI_DISABLED, "Alt+F4");
        break;
    case 2:
        if (app && app->edit.n) { ADD_DEF(app->edit); }
        else {
            ADD("Cut", 0, MI_DISABLED, 0);
            ADD("Copy", 0, MI_DISABLED, 0);
            ADD("Paste", 0, MI_DISABLED, 0);
        }
        break;
    case 3:
        if (app && app->view.n) { ADD_DEF(app->view); ADD("-", 0, 0, 0); }
        ADD("Cascade Windows", CMD_CASCADE, 0, 0);
        ADD("Tile Windows", CMD_TILE, 0, 0);
        ADD("Minimize All", CMD_MINIMIZE_ALL, 0, 0);
        break;
    case 4:
        ADD("Task Manager", CMD_TASKMAN, 0, 0);
        ADD("Cycle Input Mode", CMD_CYCLE_MODE, 0, "F2");
        ADD("-", 0, 0, 0);
        ADD("Restart...", CMD_RESTART, 0, 0);
        ADD("Shut Down...", CMD_SHUTDOWN, 0, 0);
        break;
    case 5:
        ADD("VidBew/OS Help", CMD_HELP, 0, 0);
        ADD("-", 0, 0, 0);
        ADD("About VidBew/OS...", CMD_ABOUT, 0, 0);
        break;
    }
#undef ADD
#undef ADD_DEF
}

static void dropdown_rect(int *x, int *y, int *w, int *h)
{
    int maxw = 0, hh = 4;
    for (int i = 0; i < nmrows; i++) {
        int tw = s_text_width(mrows[i].label);
        if (mrows[i].hint) tw += 28 + s_text_width(mrows[i].hint);
        if (tw > maxw) maxw = tw;
        hh += mrow_h(&mrows[i]);
    }
    *w = maxw + 34;
    if (*w < 130) *w = 130;
    *h = hh;
    *x = mb_x[menu_open];
    if (*x + *w > SCREEN_WIDTH) *x = SCREEN_WIDTH - *w;
    *y = MENUBAR_H;
}

static int dropdown_item_at(int px, int py)
{
    if (menu_open < 0) return -1;
    int x, y, w, h;
    dropdown_rect(&x, &y, &w, &h);
    if (!ui_inside(px, py, x, y, w, h)) return -1;
    int yy = y + 2;
    for (int i = 0; i < nmrows; i++) {
        int rh = mrow_h(&mrows[i]);
        if (py >= yy && py < yy + rh) return is_sep(mrows[i].label) ? -1 : i;
        yy += rh;
    }
    return -1;
}

static void damage_dropdown(void)
{
    if (menu_open < 0) return;
    int x, y, w, h;
    dropdown_rect(&x, &y, &w, &h);
    damage_add(x, y, w + 4, h + 4);
}

static void close_menus(void)
{
    if (menu_open >= 0) { damage_dropdown(); damage_add(0, 0, SCREEN_WIDTH, MENUBAR_H); }
    menu_open = -1;
    menu_hover = -1;
}

static void open_menu(int idx)
{
    close_menus();
    start_open = 0;
    menu_open = idx;
    menu_hover = -1;
    build_menu(idx);
    damage_dropdown();
    damage_add(0, 0, SCREEN_WIDTH, MENUBAR_H);
}

/* ---- start menu ---- */

static void start_rect(int *x, int *y, int *w, int *h)
{
    int hh = 4;
    for (int i = 0; i < NSITEMS; i++) hh += sitem_h(i);
    *w = START_W;
    *h = hh;
    *x = 2;
    *y = SCREEN_HEIGHT - TASKBAR_H - hh;
}

static int start_item_at(int px, int py)
{
    int x, y, w, h;
    start_rect(&x, &y, &w, &h);
    if (!ui_inside(px, py, x, y, w, h)) return -1;
    int yy = y + 2;
    for (int i = 0; i < NSITEMS; i++) {
        int rh = sitem_h(i);
        if (py >= yy && py < yy + rh) return is_sep(sitems[i].label) ? -1 : i;
        yy += rh;
    }
    return -1;
}

static void damage_start(void)
{
    int x, y, w, h;
    start_rect(&x, &y, &w, &h);
    damage_add(x, y, w, h + TASKBAR_H);
}

/* ---------------------------------------------------------------- drawing */

static void title_buttons(const window_t *w, int *close_x, int *max_x, int *min_x)
{
    int x = w->w - FRAME - 2 - BTN_W;
    *close_x = x;
    x -= 2 + BTN_W;
    *max_x = -1;
    *min_x = -1;
    if ((w->flags & WF_RESIZABLE) && !(w->flags & WF_NO_MAX)) { *max_x = x; x -= BTN_W; }
    if (!(w->flags & WF_NO_MIN)) *min_x = x;
}

static void draw_title_button(surface_t *s, int bx, int by, int kind, int pressed)
{
    s_fill(s, bx, by, BTN_W, BTN_H, COL_LIGHT_GREY);
    s_bevel(s, bx, by, BTN_W, BTN_H, pressed);
    int o = pressed ? 1 : 0;
    bx += o; by += o;
    switch (kind) {
    case 0: s_fill(s, bx + 4, by + 9, 7, 2, COL_BLACK); break;                       /* minimize */
    case 1: s_rect(s, bx + 3, by + 2, 10, 9, COL_BLACK); s_hline(s, bx + 3, by + 3, 10, COL_BLACK); break;  /* maximize */
    case 2: s_rect(s, bx + 6, by + 2, 7, 6, COL_BLACK); s_hline(s, bx + 6, by + 3, 7, COL_BLACK);          /* restore */
            s_fill(s, bx + 3, by + 5, 7, 6, COL_LIGHT_GREY);
            s_rect(s, bx + 3, by + 5, 7, 6, COL_BLACK); s_hline(s, bx + 3, by + 6, 7, COL_BLACK); break;
    case 3:                                                                           /* close */
        s_line(s, bx + 4, by + 3, bx + 11, by + 10, COL_BLACK);
        s_line(s, bx + 5, by + 3, bx + 12, by + 10, COL_BLACK);
        s_line(s, bx + 11, by + 3, bx + 4, by + 10, COL_BLACK);
        s_line(s, bx + 12, by + 3, bx + 5, by + 10, COL_BLACK);
        break;
    }
}

/* which title button (if any) is currently held down, for visual feedback */
static window_t *btn_win = 0;
static int btn_which = -1;       /* 0 min, 1 max, 3 close */

static void draw_window(surface_t *s, window_t *w)
{
    int x = w->x, y = w->y;
    int active = (w == focus_win);
    s_fill(s, x, y, w->w, w->h, COL_LIGHT_GREY);
    s_bevel(s, x, y, w->w, w->h, 0);

    int tx = x + FRAME, ty = y + FRAME, tw = w->w - 2 * FRAME;
    s_fill(s, tx, ty, tw, TITLE_H, active ? theme->title_a : theme->title_i);
    int text_x = tx + 6;
    if (w->app) { icon_draw_small(s, tx + 2, ty + 1, w->app->icon); text_x = tx + 22; }

    int cl, mx, mn;
    title_buttons(w, &cl, &mx, &mn);
    int leftmost = mn >= 0 ? mn : (mx >= 0 ? mx : cl);
    int maxc = (x + leftmost - 4 - text_x) / FONT_WIDTH;
    s_text_n(s, text_x, ty + 1, w->title, maxc, active ? theme->title_a_text : theme->title_i_text, COL_NONE);

    int by = y + FRAME + 2;
    draw_title_button(s, x + cl, by, 3, btn_win == w && btn_which == 3);
    if (mx >= 0) draw_title_button(s, x + mx, by, w->state == WS_MAX ? 2 : 1, btn_win == w && btn_which == 1);
    if (mn >= 0) draw_title_button(s, x + mn, by, 0, btn_win == w && btn_which == 0);

    s_hline(s, x + FRAME, y + FRAME + TITLE_H, w->w - 2 * FRAME, COL_DARK_GREY);

    int cx = x + FRAME, cy = y + CLIENT_DY;
    const surface_t *c = &w->client;
    if (c->px) {
        int bw = c->w < w->cw ? c->w : w->cw;
        int bh = c->h < w->ch ? c->h : w->ch;
        s_blit(s, cx, cy, c, 0, 0, bw, bh);
        if (bw < w->cw) s_fill(s, cx + bw, cy, w->cw - bw, w->ch, COL_LIGHT_GREY);
        if (bh < w->ch) s_fill(s, cx, cy + bh, bw, w->ch - bh, COL_LIGHT_GREY);
    }
}

static void dicon_pos(int i, int *x, int *y)
{
    *x = 6;
    *y = MENUBAR_H + 8 + i * DICON_H;
}

static void draw_desktop(surface_t *s)
{
    s_fill(s, s->cx0, s->cy0, s->cx1 - s->cx0, s->cy1 - s->cy0, theme->desktop);
    for (int i = 0; i < NDICONS; i++) {
        int x, y;
        dicon_pos(i, &x, &y);
        if (!rect_hit(s, x, y, DICON_W, DICON_H)) continue;
        icon_draw(s, x + (DICON_W - DICON_SIZE) / 2, y + 2, DICON_SIZE, dicons[i].icon);
        int tw = s_text_width(dicons[i].label);
        int lx = x + (DICON_W - tw) / 2, ly = y + DICON_SIZE + 6;
        if (i == desktop_sel) {
            s_fill(s, lx - 2, ly - 1, tw + 4, 18, COL_BLUE);
            s_text(s, lx, ly, dicons[i].label, COL_WHITE, COL_NONE);
        } else {
            s_text(s, lx + 1, ly + 1, dicons[i].label, COL_BLACK, COL_NONE);
            s_text(s, lx, ly, dicons[i].label, COL_WHITE, COL_NONE);
        }
    }
}

static void draw_menubar(surface_t *s)
{
    if (!rect_hit(s, 0, 0, SCREEN_WIDTH, MENUBAR_H)) return;
    s_fill(s, 0, 0, SCREEN_WIDTH, MENUBAR_H, theme->menubar);
    s_hline(s, 0, MENUBAR_H - 1, SCREEN_WIDTH, COL_BLACK);
    for (int i = 0; i < 6; i++) {
        int open = (menu_open == i);
        if (open) s_fill(s, mb_x[i], 0, mb_w[i], MENUBAR_H - 1, COL_LIGHT_GREY);
        int fg = open ? COL_BLACK : theme->menubar_text;
        int tx = mb_x[i] + 8;
        if (i == 0) { icon_draw_small(s, mb_x[i] + 5, 1, ICON_LOGO); tx += 20; }
        s_text(s, tx, 2, mb_titles[i], fg, COL_NONE);
    }
    s_text(s, mode_x(), 2, input_mode_name(), theme->menubar_text, COL_NONE);
    s_text(s, clock_x(), 2, clock_str, theme->menubar_text, COL_NONE);
}

static void draw_dropdown(surface_t *s)
{
    if (menu_open < 0) return;
    int x, y, w, h;
    dropdown_rect(&x, &y, &w, &h);
    if (!rect_hit(s, x, y, w + 4, h + 4)) return;
    s_fill(s, x + 3, y + 3, w, h, COL_DARK_GREY);
    s_fill(s, x, y, w, h, COL_LIGHT_GREY);
    s_bevel(s, x, y, w, h, 0);
    int yy = y + 2;
    for (int i = 0; i < nmrows; i++) {
        const mrow_t *r = &mrows[i];
        int rh = mrow_h(r);
        if (is_sep(r->label)) {
            s_hline(s, x + 4, yy + 3, w - 8, COL_DARK_GREY);
            s_hline(s, x + 4, yy + 4, w - 8, COL_WHITE);
        } else {
            int disabled = r->flags & MI_DISABLED;
            int hot = (i == menu_hover) && !disabled;
            if (hot) s_fill(s, x + 2, yy, w - 4, rh, COL_BLUE);
            int fg = disabled ? COL_DARK_GREY : (hot ? COL_WHITE : COL_BLACK);
            if (disabled) s_text(s, x + 17, yy + 3, r->label, COL_WHITE, COL_NONE);
            s_text(s, x + 16, yy + 2, r->label, fg, COL_NONE);
            if (r->hint) s_text(s, x + w - 10 - s_text_width(r->hint), yy + 2, r->hint, fg, COL_NONE);
        }
        yy += rh;
    }
}

static void draw_start_menu(surface_t *s)
{
    if (!start_open) return;
    int x, y, w, h;
    start_rect(&x, &y, &w, &h);
    if (!rect_hit(s, x, y, w + 4, h + 4)) return;
    s_fill(s, x, y, w, h, COL_LIGHT_GREY);
    s_bevel(s, x, y, w, h, 0);
    s_fill(s, x + 3, y + 3, 22, h - 6, theme->menubar);                   /* side banner */
    icon_draw_small(s, x + 6, y + h - 24, ICON_LOGO);
    int yy = y + 2;
    for (int i = 0; i < NSITEMS; i++) {
        int rh = sitem_h(i);
        if (is_sep(sitems[i].label)) {
            s_hline(s, x + 28, yy + 3, w - 34, COL_DARK_GREY);
            s_hline(s, x + 28, yy + 4, w - 34, COL_WHITE);
        } else {
            int hot = (i == start_hover);
            if (hot) s_fill(s, x + 26, yy, w - 30, rh, COL_BLUE);
            icon_draw_small(s, x + 32, yy + 5, sitems[i].icon);
            s_text(s, x + 56, yy + 5, sitems[i].label, hot ? COL_WHITE : COL_BLACK, COL_NONE);
        }
        yy += rh;
    }
}

/* ---- taskbar ---- */

#define START_BTN_X 3
#define START_BTN_W 84
#define TRAY_W      64

static int tray_x(void) { return SCREEN_WIDTH - TRAY_W - 3; }

static int taskbar_count(void)
{
    int n = 0;
    for (int i = 0; i < tcount; i++) if (!(taskorder[i]->flags & WF_NO_TASKBAR)) n++;
    return n;
}

static int task_btn_w(int n)
{
    int x0 = START_BTN_X + START_BTN_W + 10, x1 = tray_x() - 6;
    int bw = n > 0 ? (x1 - x0) / n : 160;
    if (bw > 160) bw = 160;
    if (bw < 28) bw = 28;
    return bw;
}

static void draw_taskbar(surface_t *s)
{
    int ty = SCREEN_HEIGHT - TASKBAR_H;
    if (!rect_hit(s, 0, ty, SCREEN_WIDTH, TASKBAR_H)) return;
    s_fill(s, 0, ty, SCREEN_WIDTH, TASKBAR_H, COL_LIGHT_GREY);
    s_hline(s, 0, ty, SCREEN_WIDTH, COL_WHITE);

    s_fill(s, START_BTN_X, ty + 3, START_BTN_W, 22, COL_LIGHT_GREY);
    s_bevel(s, START_BTN_X, ty + 3, START_BTN_W, 22, start_open);
    int so = start_open ? 1 : 0;
    icon_draw_small(s, START_BTN_X + 6 + so, ty + 6 + so, ICON_LOGO);
    s_text(s, START_BTN_X + 26 + so, ty + 6 + so, "VidBew", COL_BLACK, COL_NONE);
    s_vline(s, START_BTN_X + START_BTN_W + 4, ty + 4, 20, COL_DARK_GREY);
    s_vline(s, START_BTN_X + START_BTN_W + 5, ty + 4, 20, COL_WHITE);

    int n = taskbar_count();
    int bw = task_btn_w(n);
    int x = START_BTN_X + START_BTN_W + 10;
    for (int i = 0; i < tcount; i++) {
        window_t *w = taskorder[i];
        if (w->flags & WF_NO_TASKBAR) continue;
        int active = (w == focus_win && w->state != WS_MIN);
        int bx = x, bwid = bw - 2;
        s_fill(s, bx, ty + 3, bwid, 22, COL_LIGHT_GREY);
        s_bevel(s, bx, ty + 3, bwid, 22, active);
        int o = active ? 1 : 0;
        int tx = bx + 5 + o;
        if (w->app) { icon_draw_small(s, tx, ty + 6 + o, w->app->icon); tx += 20; }
        int maxc = (bx + bwid - 4 - tx) / FONT_WIDTH;
        if (maxc > 0) s_text_n(s, tx, ty + 6 + o, w->title, maxc, w->state == WS_MIN ? COL_DARK_GREY : COL_BLACK, COL_NONE);
        x += bw;
    }

    int tx = tray_x();
    s_fill(s, tx, ty + 3, TRAY_W, 22, COL_LIGHT_GREY);
    s_bevel1(s, tx, ty + 3, TRAY_W, 22, 1);
    icon_draw_small(s, tx + 6, ty + 6, ICON_NETWORK);
    int ns = net_tray_state();
    s_fill(s, tx + 17, ty + 17, 5, 5, ns == 2 ? COL_LIGHT_GREEN : (ns == 1 ? COL_YELLOW : COL_LIGHT_RED));
    s_rect(s, tx + 17, ty + 17, 5, 5, COL_BLACK);
    icon_type_t mi = input_mode_get() == INPUT_MODE_MOUSE ? ICON_POINTER :
                     (input_mode_get() == INPUT_MODE_KEYBOARD_NAV ? ICON_KEYBOARD : ICON_DISPLAY);
    icon_draw_small(s, tx + 38, ty + 6, mi);
}

/* ---- compose ---- */

static void compose_rect(int x0, int y0, int x1, int y1)
{
    surface_t *s = &gfx_screen;
    surf_set_clip(s, x0, y0, x1 - x0, y1 - y0);
    draw_desktop(s);
    for (int i = 0; i < zcount; i++) {
        window_t *w = zorder[i];
        if (w->state == WS_MIN) continue;
        if (!rect_hit(s, w->x, w->y, w->w, w->h)) continue;
        draw_window(s, w);
    }
    draw_menubar(s);
    draw_taskbar(s);
    draw_dropdown(s);
    draw_start_menu(s);
    surf_reset_clip(s);
    gfx_present(x0, y0, x1 - x0, y1 - y0);
}

/* ---------------------------------------------------------------- window state changes */

static void request_close(window_t *w)
{
    if (!w->close_req) w->close_req = timer_ticks ? timer_ticks : 1;
    post_event(w, EV_CLOSE, 0, 0, 0, 0);
}

static void set_state(window_t *w, int state)
{
    if (w->state == state) return;
    damage_window(w);
    int ax, ay, aw, ah;
    work_area(&ax, &ay, &aw, &ah);

    if (state == WS_MAX) {
        if (w->state == WS_NORMAL) { w->rx = w->x; w->ry = w->y; w->rcw = w->cw; w->rch = w->ch; }
        w->state = WS_MAX;
        w->x = ax; w->y = ay;
        w->w = aw; w->h = ah;
        w->cw = aw - 2 * FRAME;
        w->ch = ah - CLIENT_DY - FRAME;
        post_event(w, EV_RESIZE, w->cw, w->ch, 0, 0);
    } else if (state == WS_NORMAL) {
        int was_max = (w->state == WS_MAX);
        w->state = WS_NORMAL;
        if (was_max) {
            w->x = w->rx; w->y = w->ry; w->cw = w->rcw; w->ch = w->rch;
            win_update_outer(w);
            post_event(w, EV_RESIZE, w->cw, w->ch, 0, 0);
        }
    } else {  /* minimize */
        if (w->state == WS_NORMAL) { w->rx = w->x; w->ry = w->y; w->rcw = w->cw; w->rch = w->ch; }
        w->state = WS_MIN;
        if (w == focus_win) { focus_win = 0; focus_set(top_focusable()); }
        /* remember it was maximized so restore brings it back that way */
    }
    damage_window(w);
    damage_taskbar();
}

static void toggle_maximize(window_t *w)
{
    if (!(w->flags & WF_RESIZABLE) || (w->flags & WF_NO_MAX)) return;
    set_state(w, w->state == WS_MAX ? WS_NORMAL : WS_MAX);
}

static void restore_from_min(window_t *w)
{
    if (w->state != WS_MIN) return;
    /* a window minimized from the maximized state has its outer rect still maximized */
    w->state = (w->w >= SCREEN_WIDTH && w->x == 0) ? WS_MAX : WS_NORMAL;
    damage_window(w);
    damage_taskbar();
}

static void cascade_windows(void)
{
    int ax, ay, aw, ah, n = 0;
    work_area(&ax, &ay, &aw, &ah);
    for (int i = 0; i < tcount; i++) {
        window_t *w = taskorder[i];
        if (w->flags & (WF_TOPMOST | WF_NO_TASKBAR)) continue;
        damage_window(w);
        if (w->state == WS_MAX) set_state(w, WS_NORMAL);
        if (w->state == WS_MIN) w->state = WS_NORMAL;
        w->x = ax + 20 + (n % 10) * 28;
        w->y = ay + 12 + (n % 10) * 28;
        if (w->x + w->w > aw) w->x = aw - w->w > 0 ? aw - w->w : 0;
        if (w->y + w->h > ay + ah) w->y = ay + ah - w->h > ay ? ay + ah - w->h : ay;
        raise_window(w);
        damage_window(w);
        n++;
    }
    focus_set(top_focusable());
}

static void tile_windows(void)
{
    int ax, ay, aw, ah, n = 0;
    window_t *list[MAX_WIN];
    work_area(&ax, &ay, &aw, &ah);
    for (int i = 0; i < tcount; i++) {
        window_t *w = taskorder[i];
        if (w->flags & (WF_TOPMOST | WF_NO_TASKBAR)) continue;
        if (w->state == WS_MAX) set_state(w, WS_NORMAL);
        if (w->state == WS_MIN) w->state = WS_NORMAL;
        list[n++] = w;
    }
    if (!n) return;
    int cols = 1;
    while (cols * cols < n) cols++;
    int rows = (n + cols - 1) / cols;
    int cw = aw / cols, chh = ah / rows;
    for (int i = 0; i < n; i++) {
        window_t *w = list[i];
        damage_window(w);
        int cx = ax + (i % cols) * cw, cy = ay + (i / cols) * chh;
        if (w->flags & WF_RESIZABLE) {
            int ncw = cw - 2 * FRAME, nch = chh - CLIENT_DY - FRAME;
            if (ncw < w->min_cw) ncw = w->min_cw;
            if (nch < w->min_ch) nch = w->min_ch;
            w->cw = ncw; w->ch = nch;
            win_update_outer(w);
            post_event(w, EV_RESIZE, w->cw, w->ch, 0, 0);
        }
        w->x = cx; w->y = cy;
        raise_window(w);
        damage_window(w);
    }
    focus_set(top_focusable());
}

static void minimize_all(void)
{
    for (int i = 0; i < tcount; i++) {
        window_t *w = taskorder[i];
        if ((w->flags & (WF_NO_MIN | WF_TOPMOST)) || w->state == WS_MIN) continue;
        set_state(w, WS_MIN);
    }
}

static void cycle_window(void)
{
    if (!tcount) return;
    int start = -1;
    for (int i = 0; i < tcount; i++) if (taskorder[i] == focus_win) start = i;
    for (int k = 1; k <= tcount; k++) {
        window_t *w = taskorder[(start + k + tcount) % tcount];
        if (w->flags & (WF_NO_TASKBAR | WF_NO_FOCUS)) continue;
        if (w->state == WS_MIN) restore_from_min(w);
        raise_window(w);
        focus_set(w);
        return;
    }
}

/* ---------------------------------------------------------------- commands */

static void execute_command(int cmd)
{
    if (cmd < 0x100) {                      /* an app's own menu item */
        if (focus_win && cmd) post_event(focus_win, EV_MENU, cmd, 0, 0, 0);
        return;
    }
    switch (cmd) {
    case CMD_CLOSE:        if (focus_win) request_close(focus_win); break;
    case CMD_MINIMIZE:     if (focus_win && !(focus_win->flags & WF_NO_MIN)) set_state(focus_win, WS_MIN); break;
    case CMD_MAXIMIZE:     if (focus_win) toggle_maximize(focus_win); break;
    case CMD_NEW_EXPLORER: app_launch(APP_EXPLORER); break;
    case CMD_NEW_TERMINAL: app_launch(APP_TERMINAL); break;
    case CMD_CONTROL_PANEL: app_launch(APP_CONTROL); break;
    case CMD_TASKMAN:      app_launch(APP_TASKMAN); break;
    case CMD_CLOCK:        app_launch(APP_CLOCK); break;
    case CMD_PROGMAN:      app_launch(APP_PROGMAN); break;
    case CMD_OSK:          app_launch(APP_OSK); break;
    case CMD_ABOUT:        app_launch(APP_ABOUT); break;
    case CMD_HELP:         app_launch(APP_HELP); break;
    case CMD_NETWORK:      app_launch(APP_NETWORK); break;
    case CMD_DISPLAY:      app_launch(APP_DISPLAY); break;
    case CMD_CASCADE:      cascade_windows(); break;
    case CMD_TILE:         tile_windows(); break;
    case CMD_MINIMIZE_ALL: minimize_all(); break;
    case CMD_CYCLE_MODE:
        input_mode_cycle();
        damage_add(0, 0, SCREEN_WIDTH, MENUBAR_H);
        damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H);
        break;
    case CMD_RESTART:      dlg_confirm("Restart", "Restart VidBew/OS now?", CMD_RESTART_NOW); break;
    case CMD_SHUTDOWN:     dlg_confirm("Shut Down", "Shut down VidBew/OS now?", CMD_SHUTDOWN_NOW); break;
    case CMD_RESTART_NOW:  power_restart(); break;
    case CMD_SHUTDOWN_NOW: power_shutdown(); break;
    default:
        if (cmd >= CMD_APP_BASE && cmd < CMD_APP_BASE + APP_COUNT) app_launch(cmd - CMD_APP_BASE);
        break;
    }
}

/* ---------------------------------------------------------------- pointer routing */

#define H_NONE   0
#define H_TITLE  1
#define H_CLOSE  2
#define H_MAX    3
#define H_MIN    4
#define H_CLIENT 5
#define H_GRIP_R 6
#define H_GRIP_B 7
#define H_GRIP_C 8
#define H_BORDER 9

static int mouse_prev_buttons = 0;
static int mouse_last_x = -1, mouse_last_y = -1;
static int dblclick_ticks = 40;

typedef enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_CLIENT } drag_t;
static drag_t drag = DRAG_NONE;
static window_t *drag_win = 0;
static int drag_ox = 0, drag_oy = 0;
static int resize_x0 = 0, resize_y0 = 0, resize_cw0 = 0, resize_ch0 = 0, resize_mode = 0;
static window_t *rcapture = 0;

static unsigned long last_click_tick = 0;
static int last_click_x = -100, last_click_y = -100;

void wm_set_dblclick_ticks(int t) { dblclick_ticks = t < 10 ? 10 : (t > 100 ? 100 : t); }
int  wm_get_dblclick_ticks(void) { return dblclick_ticks; }

static window_t *window_at(int x, int y)
{
    for (int i = zcount - 1; i >= 0; i--) {
        window_t *w = zorder[i];
        if (w->state == WS_MIN) continue;
        if (ui_inside(x, y, w->x, w->y, w->w, w->h)) return w;
    }
    return 0;
}

static int classify(window_t *w, int x, int y)
{
    int lx = x - w->x, ly = y - w->y;
    int cl, mx, mn;
    title_buttons(w, &cl, &mx, &mn);
    int by = FRAME + 2;
    if (ly >= by && ly < by + BTN_H) {
        if (lx >= cl && lx < cl + BTN_W) return H_CLOSE;
        if (mx >= 0 && lx >= mx && lx < mx + BTN_W) return H_MAX;
        if (mn >= 0 && lx >= mn && lx < mn + BTN_W) return H_MIN;
    }
    if ((w->flags & WF_RESIZABLE) && w->state == WS_NORMAL) {
        int near_r = lx >= w->w - 5, near_b = ly >= w->h - 5;
        int corner_r = lx >= w->w - 16, corner_b = ly >= w->h - 16;
        if ((near_r && corner_b) || (near_b && corner_r)) return H_GRIP_C;
        if (near_r) return H_GRIP_R;
        if (near_b) return H_GRIP_B;
    }
    if (ly >= FRAME && ly < FRAME + TITLE_H && lx >= FRAME && lx < w->w - FRAME) return H_TITLE;
    if (lx >= FRAME && lx < FRAME + w->cw && ly >= CLIENT_DY && ly < CLIENT_DY + w->ch) return H_CLIENT;
    return H_BORDER;
}

static int desktop_icon_at(int x, int y)
{
    for (int i = 0; i < NDICONS; i++) {
        int ix, iy;
        dicon_pos(i, &ix, &iy);
        if (ui_inside(x, y, ix, iy, DICON_W, DICON_H)) return i;
    }
    return -1;
}

static void select_desktop_icon(int i)
{
    if (i == desktop_sel) return;
    for (int k = 0; k < 2; k++) {
        int idx = k ? i : desktop_sel;
        if (idx >= 0) { int ix, iy; dicon_pos(idx, &ix, &iy); damage_add(ix, iy, DICON_W, DICON_H); }
    }
    desktop_sel = i;
}

static void clamp_window_pos(window_t *w)
{
    int ax, ay, aw, ah;
    work_area(&ax, &ay, &aw, &ah);
    if (w->y < ay) w->y = ay;
    if (w->y > ay + ah - TITLE_H - FRAME) w->y = ay + ah - TITLE_H - FRAME;
    if (w->x < -(w->w - 60)) w->x = -(w->w - 60);
    if (w->x > aw - 60) w->x = aw - 60;
}

static void taskbar_press(int x, int y)
{
    if (x >= START_BTN_X && x < START_BTN_X + START_BTN_W) {
        if (start_open) { damage_start(); start_open = 0; start_hover = -1; }
        else { close_menus(); start_open = 1; start_hover = -1; damage_start(); }
        damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H);
        return;
    }
    if (x >= tray_x()) {
        if (x < tray_x() + 32) execute_command(CMD_NETWORK);
        else execute_command(CMD_CYCLE_MODE);
        return;
    }
    int n = taskbar_count();
    int bw = task_btn_w(n);
    int bx = START_BTN_X + START_BTN_W + 10;
    for (int i = 0; i < tcount; i++) {
        window_t *w = taskorder[i];
        if (w->flags & WF_NO_TASKBAR) continue;
        if (x >= bx && x < bx + bw - 2) {
            if (w->state == WS_MIN) { restore_from_min(w); raise_window(w); focus_set(w); }
            else if (w == focus_win) set_state(w, WS_MIN);
            else { raise_window(w); focus_set(w); }
            damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H);
            return;
        }
        bx += bw;
    }
}

static void window_press(window_t *w, int x, int y, int dbl)
{
    raise_window(w);
    if (!(w->flags & WF_NO_FOCUS)) focus_set(w);
    int hit = classify(w, x, y);
    switch (hit) {
    case H_CLOSE: btn_win = w; btn_which = 3; damage_add(w->x, w->y, w->w, CLIENT_DY); break;
    case H_MAX:   btn_win = w; btn_which = 1; damage_add(w->x, w->y, w->w, CLIENT_DY); break;
    case H_MIN:   btn_win = w; btn_which = 0; damage_add(w->x, w->y, w->w, CLIENT_DY); break;
    case H_TITLE:
        if (dbl) { toggle_maximize(w); last_click_tick = 0; }
        else if (w->state != WS_MAX) {
            drag = DRAG_MOVE; drag_win = w; drag_ox = x - w->x; drag_oy = y - w->y;
        }
        break;
    case H_GRIP_R: case H_GRIP_B: case H_GRIP_C:
        drag = DRAG_RESIZE; drag_win = w; resize_mode = hit;
        resize_x0 = x; resize_y0 = y; resize_cw0 = w->cw; resize_ch0 = w->ch;
        break;
    case H_CLIENT:
        drag = DRAG_CLIENT; drag_win = w;
        post_event(w, EV_MOUSE_DOWN, x - (w->x + FRAME), y - (w->y + CLIENT_DY), 1, dbl);
        break;
    default: break;
    }
}

static void on_left_down(int x, int y)
{
    int dbl = (timer_ticks - last_click_tick <= (unsigned long)dblclick_ticks) &&
              (x - last_click_x <= 4 && last_click_x - x <= 4 && y - last_click_y <= 4 && last_click_y - y <= 4);
    last_click_tick = dbl ? 0 : timer_ticks;
    last_click_x = x;
    last_click_y = y;

    if (menu_open >= 0) {
        int item = dropdown_item_at(x, y);
        if (item >= 0) { menu_hover = item; damage_dropdown(); return; }
        if (y < MENUBAR_H) {
            for (int i = 0; i < 6; i++)
                if (x >= mb_x[i] && x < mb_x[i] + mb_w[i]) {
                    if (i == menu_open) close_menus(); else open_menu(i);
                    return;
                }
        }
        int dx, dy, dw, dh;
        dropdown_rect(&dx, &dy, &dw, &dh);
        if (!ui_inside(x, y, dx, dy, dw, dh)) close_menus();
        return;
    }
    if (start_open) {
        int item = start_item_at(x, y);
        if (item >= 0) { start_hover = item; damage_start(); return; }
        if (y >= SCREEN_HEIGHT - TASKBAR_H && x >= START_BTN_X && x < START_BTN_X + START_BTN_W) { taskbar_press(x, y); return; }
        damage_start();
        start_open = 0;
        start_hover = -1;
        damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H);
        return;
    }
    if (y < MENUBAR_H) {
        for (int i = 0; i < 6; i++)
            if (x >= mb_x[i] && x < mb_x[i] + mb_w[i]) { open_menu(i); return; }
        return;
    }
    if (y >= SCREEN_HEIGHT - TASKBAR_H) { taskbar_press(x, y); return; }

    window_t *w = window_at(x, y);
    if (w) { window_press(w, x, y, dbl); return; }

    int ic = desktop_icon_at(x, y);
    select_desktop_icon(ic);
    if (ic >= 0 && dbl) execute_command(dicons[ic].cmd);
    if (ic < 0 && focus_win) focus_set(0);
}

static void on_left_up(int x, int y)
{
    if (menu_open >= 0) {
        int item = dropdown_item_at(x, y);
        if (item >= 0) {
            int cmd = mrows[item].cmd, disabled = mrows[item].flags & MI_DISABLED;
            if (!disabled) { close_menus(); execute_command(cmd); }
            return;
        }
        if (y >= MENUBAR_H) close_menus();
        return;
    }
    if (start_open) {
        int item = start_item_at(x, y);
        if (item >= 0) {
            int cmd = sitems[item].cmd;
            damage_start();
            start_open = 0;
            start_hover = -1;
            damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H);
            execute_command(cmd);
        }
        return;
    }
    if (btn_win) {
        window_t *w = btn_win;
        int which = btn_which;
        btn_win = 0;
        btn_which = -1;
        damage_add(w->x, w->y, w->w, CLIENT_DY);
        int hit = classify(w, x, y);
        if (hit == H_CLOSE && which == 3) request_close(w);
        else if (hit == H_MAX && which == 1) toggle_maximize(w);
        else if (hit == H_MIN && which == 0) set_state(w, WS_MIN);
        return;
    }
    if (drag == DRAG_CLIENT && drag_win && drag_win->used)
        post_event(drag_win, EV_MOUSE_UP, x - (drag_win->x + FRAME), y - (drag_win->y + CLIENT_DY), 1, 0);
    drag = DRAG_NONE;
    drag_win = 0;
}

static void on_mouse_move(int x, int y, int buttons)
{
    if (drag == DRAG_MOVE && drag_win) {
        window_t *w = drag_win;
        damage_window(w);
        w->x = x - drag_ox;
        w->y = y - drag_oy;
        clamp_window_pos(w);
        damage_window(w);
    } else if (drag == DRAG_RESIZE && drag_win) {
        window_t *w = drag_win;
        int ncw = resize_cw0, nch = resize_ch0;
        if (resize_mode != H_GRIP_B) ncw += x - resize_x0;
        if (resize_mode != H_GRIP_R) nch += y - resize_y0;
        int ax, ay, aw, ah;
        work_area(&ax, &ay, &aw, &ah);
        if (ncw < w->min_cw) ncw = w->min_cw;
        if (nch < w->min_ch) nch = w->min_ch;
        if (ncw > aw - 2 * FRAME) ncw = aw - 2 * FRAME;
        if (nch > ah) nch = ah;
        if (ncw != w->cw || nch != w->ch) {
            damage_window(w);
            w->cw = ncw; w->ch = nch;
            win_update_outer(w);
            damage_window(w);
            post_event(w, EV_RESIZE, ncw, nch, 0, 0);
        }
    } else if (drag == DRAG_CLIENT && drag_win && drag_win->used) {
        post_event(drag_win, EV_MOUSE_MOVE, x - (drag_win->x + FRAME), y - (drag_win->y + CLIENT_DY), buttons & 7, 0);
    } else if (menu_open >= 0) {
        int item = dropdown_item_at(x, y);
        if (item != menu_hover) { menu_hover = item; damage_dropdown(); }
        if (y < MENUBAR_H)
            for (int i = 0; i < 6; i++)
                if (i != menu_open && x >= mb_x[i] && x < mb_x[i] + mb_w[i]) { open_menu(i); break; }
    } else if (start_open) {
        int item = start_item_at(x, y);
        if (item != start_hover) { start_hover = item; damage_start(); }
    }
}

static void handle_mouse_event(const mouse_event_t *e)
{
#ifdef WM_TRACE
    { char m[64]; ksnprintf(m, sizeof(m), "mouse %d %d b%d\n", e->x, e->y, e->buttons); serial_print(m); }
#endif
    if (e->x != mouse_last_x || e->y != mouse_last_y) {
        mouse_last_x = e->x;
        mouse_last_y = e->y;
        on_mouse_move(e->x, e->y, e->buttons);
    }
    int pressed = e->buttons & ~mouse_prev_buttons;
    int released = mouse_prev_buttons & ~e->buttons;
    mouse_prev_buttons = e->buttons;

    if (pressed & 1) on_left_down(e->x, e->y);
    if (released & 1) on_left_up(e->x, e->y);

    if (pressed & 2) {
        window_t *w = (menu_open < 0 && !start_open && e->y >= MENUBAR_H && e->y < SCREEN_HEIGHT - TASKBAR_H)
                      ? window_at(e->x, e->y) : 0;
        if (w && classify(w, e->x, e->y) == H_CLIENT) {
            raise_window(w);
            if (!(w->flags & WF_NO_FOCUS)) focus_set(w);
            rcapture = w;
            post_event(w, EV_MOUSE_DOWN, e->x - (w->x + FRAME), e->y - (w->y + CLIENT_DY), 2, 0);
        }
    }
    if ((released & 2) && rcapture) {
        if (rcapture->used)
            post_event(rcapture, EV_MOUSE_UP, e->x - (rcapture->x + FRAME), e->y - (rcapture->y + CLIENT_DY), 2, 0);
        rcapture = 0;
    }
}

/* ---------------------------------------------------------------- keyboard routing */

static void menu_key(int code)
{
    switch (code) {
    case 27: close_menus(); break;
    case KEY_LEFT: open_menu((menu_open + 5) % 6); break;
    case KEY_RIGHT: open_menu((menu_open + 1) % 6); break;
    case KEY_UP: case KEY_DOWN: {
        int step = code == KEY_UP ? nmrows - 1 : 1, i = menu_hover < 0 ? (code == KEY_UP ? 0 : nmrows - 1) : menu_hover;
        for (int k = 0; k < nmrows; k++) {
            i = (i + step) % nmrows;
            if (!is_sep(mrows[i].label) && !(mrows[i].flags & MI_DISABLED)) break;
        }
        menu_hover = i;
        damage_dropdown();
        break;
    }
    case '\n':
        if (menu_hover >= 0) {
            int cmd = mrows[menu_hover].cmd;
            if (!(mrows[menu_hover].flags & MI_DISABLED)) { close_menus(); execute_command(cmd); }
        }
        break;
    default: break;
    }
}

static void handle_key(const key_event_t *k)
{
    if (k->code == KEY_F1) { execute_command(CMD_OSK); return; }
    if (k->code == KEY_F2) { execute_command(CMD_CYCLE_MODE); return; }
    if ((k->mods & MOD_ALT) && k->code == '\t') { cycle_window(); return; }
    if ((k->mods & MOD_ALT) && k->code == KEY_F4) { execute_command(CMD_CLOSE); return; }
    if (k->code == KEY_WIN) {
        if (start_open) { damage_start(); start_open = 0; }
        else { close_menus(); start_open = 1; start_hover = -1; damage_start(); }
        damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H);
        return;
    }
    if (menu_open >= 0) { menu_key(k->code); return; }
    if (start_open) {
        if (k->code == 27) { damage_start(); start_open = 0; damage_add(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H); }
        return;
    }
    if (focus_win) post_event(focus_win, EV_KEY, k->code, k->mods, 0, 0);
}

/* ---------------------------------------------------------------- display changes */

static volatile int disp_state = 0;       /* 0 idle, 1 requested, 2 ok, 3 failed */
static int disp_w, disp_h, disp_bpp;

int wm_change_display(int w, int h, int bpp)
{
    disp_w = w; disp_h = h; disp_bpp = bpp;
    disp_state = 1;
    while (disp_state == 1) task_sleep(1);
    int ok = (disp_state == 2);
    disp_state = 0;
    return ok;
}

static void display_changed(void)
{
    mouse_set_bounds(SCREEN_WIDTH, SCREEN_HEIGHT);
    close_menus();
    start_open = 0;
    int ax, ay, aw, ah;
    work_area(&ax, &ay, &aw, &ah);
    for (int i = 0; i < tcount; i++) {
        window_t *w = taskorder[i];
        if (w->state == WS_MAX) {
            w->x = ax; w->y = ay; w->w = aw; w->h = ah;
            w->cw = aw - 2 * FRAME; w->ch = ah - CLIENT_DY - FRAME;
            post_event(w, EV_RESIZE, w->cw, w->ch, 0, 0);
        } else if (w->flags & WF_TOPMOST) {
            w->x = (SCREEN_WIDTH - w->w) / 2;
            w->y = SCREEN_HEIGHT - TASKBAR_H - w->h;
        } else {
            if (w->x + w->w > aw) w->x = aw - w->w;
            if (w->y + w->h > ay + ah) w->y = ay + ah - w->h;
            if (w->x < 0) w->x = 0;
            if (w->y < ay) w->y = ay;
        }
        post_event(w, EV_REPAINT, 0, 0, 0, 0);
    }
    damage_all();
}

/* ---------------------------------------------------------------- the UI iteration */

static unsigned long last_rtc_tick = 0;

static void wm_tick(void)
{
    if (timer_ticks - last_rtc_tick >= 25) {
        last_rtc_tick = timer_ticks;
        int h, m, s;
        rtc_read_time(&h, &m, &s);
        if (s != last_sec) {
            last_sec = s;
            ksnprintf(clock_str, sizeof(clock_str), "%02d:%02d:%02d", h, m, s);
            damage_add(mode_x() - 4, 0, SCREEN_WIDTH - mode_x() + 4, MENUBAR_H);
        }
    }
    static int last_net = -1, last_mode = -1;
    int ns = net_tray_state(), md = (int)input_mode_get();
    if (ns != last_net || md != last_mode) {
        last_net = ns;
        last_mode = md;
        damage_add(0, 0, SCREEN_WIDTH, MENUBAR_H);
        damage_add(tray_x(), SCREEN_HEIGHT - TASKBAR_H, TRAY_W + 3, TASKBAR_H);
    }
    for (int i = 0; i < MAX_WIN; i++) {      /* unresponsive apps: force-close after ~3s */
        window_t *w = &wins[i];
        if (w->used && w->close_req && timer_ticks - w->close_req > 300) {
            serial_print("wm: window did not close in time, ending its task\n");
            w->close_req = 0;
            task_kill(w->owner);
        }
    }
}

static void collect_app_damage(void)
{
    for (int i = 0; i < MAX_WIN; i++) {
        window_t *w = &wins[i];
        if (!w->used || !w->dirty) continue;
        u32 f = irq_save();
        int x0 = w->dx0, y0 = w->dy0, x1 = w->dx1, y1 = w->dy1;
        w->dirty = 0;
        irq_restore(f);
        if (w->state == WS_MIN) continue;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w->cw) x1 = w->cw;
        if (y1 > w->ch) y1 = w->ch;
        if (x1 > x0 && y1 > y0) damage_add(w->x + FRAME + x0, w->y + CLIENT_DY + y0, x1 - x0, y1 - y0);
    }
}

void wm_init(void)
{
    mutex_init(&wm_lock);
    menubar_layout();
    mouse_set_bounds(SCREEN_WIDTH, SCREEN_HEIGHT);
    int h, m, s;
    rtc_read_time(&h, &m, &s);
    ksnprintf(clock_str, sizeof(clock_str), "%02d:%02d:%02d", h, m, s);
    last_sec = s;
    damage_all();
}

void wm_set_scheme(int i)
{
    if (i < 0 || i >= SCHEME_COUNT) return;
    mutex_lock(&wm_lock);
    scheme_idx = i;
    theme = &schemes[i];
    for (int k = 0; k < tcount; k++) post_event(taskorder[k], EV_REPAINT, 0, 0, 0, 0);
    damage_all();
    mutex_unlock(&wm_lock);
}

void wm_close_by_owner(int task_id)
{
    mutex_lock(&wm_lock);
    int any = 0;
    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].used && wins[i].owner == task_id) { request_close(&wins[i]); any = 1; }
    mutex_unlock(&wm_lock);
    if (!any) task_kill(task_id);
}

int wm_net_status(void) { return net_tray_state(); }

void wm_request_close(window_t *w)
{
    mutex_lock(&wm_lock);
    if (w->used) request_close(w);
    mutex_unlock(&wm_lock);
}

void wm_ui_iteration(void)
{
    mutex_lock(&wm_lock);

    key_event_t k;
    while (keyboard_get_event(&k)) handle_key(&k);

    mouse_event_t e;
    while (mouse_get_event(&e)) handle_mouse_event(&e);

    int cmd;
    while (cmdq_pop(&cmd)) execute_command(cmd);

    if (disp_state == 1) {
        if (gfx_set_mode_live(disp_w, disp_h, disp_bpp)) { display_changed(); disp_state = 2; }
        else disp_state = 3;
    }

    wm_tick();
    collect_app_damage();

    /* pointer first, so a present that touches it repaints it in the right place */
    gfx_cursor_show(input_mode_cursor_visible());
    gfx_cursor_move(mouse_x, mouse_y);

    for (int i = 0; i < ndmg; i++) compose_rect(dmg[i].x0, dmg[i].y0, dmg[i].x1, dmg[i].y1);
    ndmg = 0;

    mutex_unlock(&wm_lock);
}
