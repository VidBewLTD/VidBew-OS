/* apps.c -- app registry, dialogs and power control. */

#include "apps.h"
#include "task.h"
#include "heap.h"
#include "kstd.h"
#include "timer.h"
#include "keyboard.h"
#include "io.h"
#include "ui.h"
#include "serial.h"
#include "gfx.h"

typedef struct {
    const char *name;
    icon_type_t icon;
    void (*entry)(void *);
    const app_def_t *def;
    int single;
} app_info_t;

static const app_info_t apps[APP_COUNT] = {
    [APP_PROGMAN]  = { "Program Manager",  ICON_LOGO,     app_progman_main,    &progman_def,  1 },
    [APP_EXPLORER] = { "File Explorer",    ICON_FOLDER,   app_explorer_main,   &explorer_def, 0 },
    [APP_TERMINAL] = { "Terminal",         ICON_MONITOR,  app_terminal_main,   &terminal_def, 0 },
    [APP_CONTROL]  = { "Control Panel",    ICON_CONTROL,  app_control_main,    &control_def,  1 },
    [APP_TASKMAN]  = { "Task Manager",     ICON_TASKS,    app_taskman_main,    &taskman_def,  1 },
    [APP_CLOCK]    = { "Clock",            ICON_CLOCK,    app_clock_main,      &clock_def,    0 },
    [APP_OSK]      = { "Keyboard",         ICON_KEYBOARD, app_osk_main,        &osk_def,      1 },
    [APP_ABOUT]    = { "About VidBew/OS",  ICON_INFO,     app_about_main,      &about_def,    1 },
    [APP_HELP]     = { "Help",             ICON_HELP,     app_help_main,       &help_def,     1 },
    [APP_DISPLAY]  = { "Display",          ICON_DISPLAY,  applet_display_main, &display_def,  1 },
    [APP_NETWORK]  = { "Network",          ICON_NETWORK,  applet_network_main, &network_def,  1 },
    [APP_INPUT]    = { "Input",            ICON_POINTER,  applet_input_main,   &input_def,    1 },
    [APP_SOUND]    = { "Sound",            ICON_DISC,     applet_sound_main,   &sound_def,    1 },
    [APP_DATETIME] = { "Date & Time",      ICON_CALENDAR, applet_datetime_main, &datetime_def, 1 },
    [APP_SYSTEM]   = { "System",           ICON_SYSTEM,   applet_system_main,  &system_def,   1 },
    [APP_CALC]     = { "Calculator",       ICON_CONTROL,  app_calc_main,       &calc_def,     0 },
    [APP_CALENDAR] = { "Calendar",         ICON_CALENDAR, app_cal_main,        &cal_def,      1 },
    [APP_ALARM]    = { "Alarm",            ICON_CLOCK,    app_alarm_main,      &alarm_def,    1 },
    [APP_PAINT]    = { "Paint",            ICON_DISC,     app_paint_main,      &paint_def,    0 },
    [APP_VIEWER]   = { "Image Viewer",     ICON_DISPLAY,  app_view_main,       &view_def,     0 },
    [APP_NOTEPAD]  = { "Text Editor",      ICON_FILE,     app_notepad_main,    &notepad_def,  0 },
};

static unsigned long launch_tick[APP_COUNT];

const char *app_name(int id) { return (id >= 0 && id < APP_COUNT) ? apps[id].name : ""; }
icon_type_t app_icon(int id) { return (id >= 0 && id < APP_COUNT) ? apps[id].icon : ICON_FILE; }

int app_launch_arg(int id, void *arg)
{
    if (id < 0 || id >= APP_COUNT) return 0;
    const app_info_t *a = &apps[id];
    if (a->single) {
        window_t *w = wm_find_by_app(a->def);
        if (w) {
            if (id == APP_OSK) wm_request_close(w);       /* the keyboard toggles */
            else wm_activate(w);
            return 1;
        }
        /* a launch is already in flight (the window isn't up yet): ignore the repeat */
        if (launch_tick[id] && timer_ticks - launch_tick[id] < 80) return 1;
    }
    launch_tick[id] = timer_ticks ? timer_ticks : 1;
    return task_create(a->name, a->entry, arg, 0) >= 0;
}

int app_launch(int id) { return app_launch_arg(id, 0); }

/* ---------------------------------------------------------------- dialogs */

#define DLG_MAX_LINES 6

int dlg_message(const char *title, const char *text, int buttons, int timeout_ticks)
{
    static const char *labels[3][2] = { { "OK", 0 }, { "Yes", "No" }, { "Keep", "Revert" } };
    int nbtn = buttons == 0 ? 1 : 2;

    char lines[DLG_MAX_LINES][64];
    int nl = 0, maxw = 0;
    {
        const char *p = text;
        while (*p && nl < DLG_MAX_LINES) {
            int n = 0;
            while (*p && *p != '\n' && n < 63) lines[nl][n++] = *p++;
            lines[nl][n] = 0;
            if (*p == '\n') p++;
            if (n * FONT_WIDTH > maxw) maxw = n * FONT_WIDTH;
            nl++;
        }
    }
    int cw = maxw + 56;
    if (cw < 260) cw = 260;
    int ch = 28 + nl * 18 + (timeout_ticks ? 20 : 0) + 56;

    window_t *w = wm_create(title, cw, ch, WF_NO_MIN | WF_TOPMOST, 0);
    if (!w) return nbtn - 1;
    wm_center(w);
    surface_t *s = wm_surface(w);

    int result = -1, pressed = -1;
    unsigned long start = timer_ticks;
    int last_left = -1;
    int bw = 84, bh = 26, gap = 14;
    int total_w = nbtn * bw + (nbtn - 1) * gap;
    int bx0 = (cw - total_w) / 2, by = ch - 40;

    while (result < 0) {
        int left = timeout_ticks ? (int)((start + (unsigned)timeout_ticks - timer_ticks + 99) / 100) : 0;
        if (left < 0) left = 0;
        s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
        for (int i = 0; i < nl; i++) s_text(s, 28, 16 + i * 18, lines[i], COL_BLACK, COL_NONE);
        if (timeout_ticks) {
            char m[48];
            ksnprintf(m, sizeof(m), "Choosing %s automatically in %d s", labels[buttons][nbtn - 1], left);
            s_text(s, 28, 16 + nl * 18 + 4, m, COL_DARK_GREY, COL_NONE);
        }
        for (int i = 0; i < nbtn; i++)
            ui_button(s, bx0 + i * (bw + gap), by, bw, bh, labels[buttons][i], pressed == i, 0);
        wm_invalidate(w);
        last_left = left;

        event_t ev;
        int got = wm_next_event(w, &ev, 25);
        if (!got) {
            if (timeout_ticks && timer_ticks - start >= (unsigned)timeout_ticks) result = nbtn - 1;
            continue;
        }
        if (ev.type == EV_CLOSE) result = nbtn - 1;
        else if (ev.type == EV_KEY) {
            if (ev.a == '\n') result = 0;
            else if (ev.a == 27) result = nbtn - 1;
        } else if (ev.type == EV_MOUSE_DOWN && ev.c == 1) {
            pressed = -1;
            for (int i = 0; i < nbtn; i++)
                if (ui_inside(ev.a, ev.b, bx0 + i * (bw + gap), by, bw, bh)) pressed = i;
        } else if (ev.type == EV_MOUSE_UP && ev.c == 1) {
            for (int i = 0; i < nbtn; i++)
                if (pressed == i && ui_inside(ev.a, ev.b, bx0 + i * (bw + gap), by, bw, bh)) result = i;
            pressed = -1;
        }
    }
    (void)last_left;
    wm_destroy(w);
    return result;
}

typedef struct { char title[40]; char text[160]; int cmd; int buttons; } confirm_t;

static void confirm_task(void *arg)
{
    confirm_t *c = (confirm_t *)arg;
    int r = dlg_message(c->title, c->text, c->buttons, 0);
    if (r == 0 && c->cmd) wm_post_command(c->cmd);
    kfree(c);
}

void dlg_confirm(const char *title, const char *text, int cmd_on_yes)
{
    confirm_t *c = (confirm_t *)kzalloc(sizeof(confirm_t));
    if (!c) return;
    kstrlcpy(c->title, title, sizeof(c->title));
    kstrlcpy(c->text, text, sizeof(c->text));
    c->cmd = cmd_on_yes;
    c->buttons = 1;
    if (task_create("dialog", confirm_task, c, 0) < 0) kfree(c);
}

/* an OK-only message box in its own task, so the caller keeps running */
void dlg_notice(const char *title, const char *text)
{
    confirm_t *c = (confirm_t *)kzalloc(sizeof(confirm_t));
    if (!c) return;
    kstrlcpy(c->title, title, sizeof(c->title));
    kstrlcpy(c->text, text, sizeof(c->text));
    c->buttons = 0;
    if (task_create("dialog", confirm_task, c, 0) < 0) kfree(c);
}

/* ---------------------------------------------------------------- power */

void power_restart(void)
{
    serial_print("power: restarting\n");
    __asm__ volatile("cli");
    for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++);
    outb(0x64, 0xFE);                       /* pulse the CPU reset line via the keyboard controller */
    struct { unsigned short limit; unsigned int base; } __attribute__((packed)) empty = { 0, 0 };
    __asm__ volatile("lidt %0; int3" : : "m"(empty));   /* fallback: triple fault */
    for (;;) __asm__ volatile("hlt");
}

void power_shutdown(void)
{
    serial_print("power: shutting down\n");
    __asm__ volatile("cli");
    outw(0x604, 0x2000);                    /* QEMU (PIIX4 ACPI) */
    outw(0xB004, 0x2000);                   /* older QEMU / Bochs */
    outw(0x4004, 0x3400);                   /* VirtualBox */
    /* Still here: the machine can't power itself off. Say so. */
    surface_t *s = &gfx_screen;
    s_fill(s, 0, 0, s->w, s->h, COL_BLACK);
    const char *l1 = "It is now safe to turn off your computer.";
    const char *l2 = "VidBew/OS";
    s_text(s, (s->w - s_text_width(l1)) / 2, s->h / 2 - 8, l1, COL_YELLOW, COL_NONE);
    s_text(s, (s->w - s_text_width(l2)) / 2, s->h / 2 + 16, l2, COL_DARK_GREY, COL_NONE);
    gfx_cursor_show(0);
    gfx_present(0, 0, s->w, s->h);
    for (;;) __asm__ volatile("cli; hlt");
}
