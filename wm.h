#ifndef WM_H
#define WM_H

/* wm.h -- the Nova window manager.
 *
 * Every window belongs to an app that runs as its own preemptively scheduled
 * task. An app draws into its window's private client surface and calls
 * wm_invalidate(); only the UI task (the compositor) ever touches the screen.
 * Input arrives as events in the window's queue: apps block in
 * wm_next_event() and wake the instant something happens. */

#include "types.h"
#include "gfx.h"
#include "icons.h"

/* ---- events ---- */
#define EV_NONE        0
#define EV_KEY         1   /* a = key code (see keyboard.h), b = modifiers */
#define EV_MOUSE_DOWN  2   /* a,b = client x,y   c = button (1 left, 2 right)   d = 1 on double-click */
#define EV_MOUSE_UP    3   /* a,b = client x,y   c = button */
#define EV_MOUSE_MOVE  4   /* a,b = client x,y   c = buttons held (only while dragging in this window) */
#define EV_CLOSE       5   /* the user asked to close the window: clean up and return */
#define EV_RESIZE      6   /* a,b = new client size; call wm_resize_client() then redraw */
#define EV_MENU        7   /* a = command id chosen from the menu bar */
#define EV_FOCUS       8
#define EV_BLUR        9
#define EV_REPAINT     10  /* redraw everything (colour scheme or display changed) */

typedef struct { int type, a, b, c, d; } event_t;

/* ---- window flags ---- */
#define WF_RESIZABLE  0x01
#define WF_NO_MIN     0x02
#define WF_NO_MAX     0x04
#define WF_NO_TASKBAR 0x08   /* no taskbar button */
#define WF_TOPMOST    0x10   /* stays above normal windows */
#define WF_NO_FOCUS   0x20   /* clicking it never steals keyboard focus (on-screen keyboard) */

/* ---- menus ---- */
#define MI_DISABLED 1
typedef struct { const char *label; int cmd; int flags; const char *hint; } menu_item_t; /* label "-" = separator */
typedef struct { int n; const menu_item_t *items; } menu_def_t;
typedef struct app_def {
    const char *name;
    icon_type_t icon;
    menu_def_t file, edit, view;   /* the app's own items, merged into the standard menus */
} app_def_t;

/* Commands below 0x100 belong to apps (delivered to the focused window as
 * EV_MENU); the rest are handled by the window manager itself. */
#define CMD_CLOSE          0x100
#define CMD_MINIMIZE       0x101
#define CMD_MAXIMIZE       0x102
#define CMD_NEW_EXPLORER   0x110
#define CMD_NEW_TERMINAL   0x111
#define CMD_CONTROL_PANEL  0x112
#define CMD_TASKMAN        0x113
#define CMD_CLOCK          0x114
#define CMD_PROGMAN        0x115
#define CMD_OSK            0x116
#define CMD_ABOUT          0x117
#define CMD_HELP           0x118
#define CMD_NETWORK        0x119
#define CMD_DISPLAY        0x11A
#define CMD_CASCADE        0x120
#define CMD_TILE           0x121
#define CMD_MINIMIZE_ALL   0x122
#define CMD_CYCLE_MODE     0x123
#define CMD_APP_BASE       0x200   /* + app id: launch that app */
#define CMD_RESTART        0x130
#define CMD_SHUTDOWN       0x131
#define CMD_RESTART_NOW    0x132
#define CMD_SHUTDOWN_NOW   0x133

typedef struct window window_t;

/* ---- for apps ---- */
window_t  *wm_create(const char *title, int cw, int ch, unsigned flags, const app_def_t *app);
window_t  *wm_create_at(const char *title, int x, int y, int cw, int ch, unsigned flags, const app_def_t *app);
void       wm_destroy(window_t *w);
surface_t *wm_surface(window_t *w);
void       wm_invalidate(window_t *w);
void       wm_invalidate_rect(window_t *w, int x, int y, int cw, int ch);
int        wm_next_event(window_t *w, event_t *ev, int timeout_ticks);   /* -1 = wait forever; 0 = timed out */
void       wm_set_title(window_t *w, const char *title);
void       wm_set_min_size(window_t *w, int min_cw, int min_ch);
int        wm_resize_client(window_t *w, int cw, int ch);                 /* app-side buffer swap */
void       wm_client_size(window_t *w, int *cw, int *ch);
void       wm_outer_size(window_t *w, int *ow, int *oh);                  /* including frame and title bar */
void       wm_set_pos(window_t *w, int x, int y);
void       wm_center(window_t *w);
void       wm_dock_bottom(window_t *w);                                   /* flush above the taskbar, centred */
int        wm_is_focused(window_t *w);
void       wm_activate(window_t *w);                                      /* restore + raise + focus */
window_t  *wm_find_by_app(const app_def_t *app);
void       wm_post_key(int code, int mods);                               /* to the focused window (on-screen keyboard) */
void       wm_post_command(int cmd);                                      /* ask the UI task to run a command */
int        wm_window_count(void);
int        wm_window_info(int index, char *title, int cap, int *owner_task, int *state);  /* for Task Manager */
void       wm_request_close(window_t *w);                                 /* polite: posts EV_CLOSE, force-ends the task if ignored */
void       wm_close_by_owner(int task_id);                                /* polite close, then force */
int        wm_change_display(int w, int h, int bpp);                      /* blocks; 1 = switched (live) */

/* clipboard (plain text) */
void       clipboard_set(const char *text);
const char *clipboard_get(void);

/* colour schemes */
int        wm_scheme_count(void);
const char *wm_scheme_name(int i);
void       wm_set_scheme(int i);
int        wm_get_scheme(void);
void       wm_scheme_swatch(int i, int *desktop, int *title);

void       wm_set_dblclick_ticks(int ticks);
int        wm_get_dblclick_ticks(void);

/* PS3-style Notifications */
void wm_notify(icon_type_t icon, const char *title, const char *msg);

/* ---- for the kernel ---- */
void wm_init(void);
void wm_ui_iteration(void);            /* one pass of the UI loop (input, damage, compose) */
void wm_task_exited(int task_id);      /* task_exit_hook target */
int  wm_net_status(void);              /* tray icon state: 0 none, 1 no address, 2 ready (set by net) */

#define MENUBAR_H 20
#define TASKBAR_H 28

#endif
