#ifndef APPS_H
#define APPS_H

#include "wm.h"

/* The app registry. Each app is a task entry function that creates its own
 * window(s) and runs an event loop until it receives EV_CLOSE. */

enum {
    APP_PROGMAN, APP_EXPLORER, APP_TERMINAL, APP_CONTROL, APP_TASKMAN, APP_CLOCK,
    APP_OSK, APP_ABOUT, APP_HELP,
    APP_DISPLAY, APP_NETWORK, APP_INPUT, APP_SOUND, APP_DATETIME, APP_SYSTEM,
    APP_CALC, APP_CALENDAR, APP_ALARM, APP_PAINT, APP_VIEWER, APP_NOTEPAD, APP_INSTALLER,
    APP_COUNT
};

int  app_launch(int id);                    /* start (or focus, for single-instance apps) */
int  app_launch_arg(int id, void *arg);
const char *app_name(int id);
icon_type_t app_icon(int id);

/* a modal-style message box that runs in its own task; on Yes/OK it posts
 * `cmd_on_yes` to the window manager (0 = none) */
void dlg_confirm(const char *title, const char *text, int cmd_on_yes);
void dlg_notice(const char *title, const char *text);      /* OK-only, non-blocking */

/* blocking dialog, for use from an app's own task. buttons: 0 = OK, 1 = Yes/No, 2 = Keep/Revert.
 * Returns the chosen button (0 = first) or the default (last) after timeout_ticks (0 = no timeout). */
int  dlg_message(const char *title, const char *text, int buttons, int timeout_ticks);

/* shared helpers */
void power_restart(void);
void power_shutdown(void);
int  app_read_file_to_cache(const char *name);

/* entry points (registered in apps.c) */
void app_progman_main(void *arg);
void app_explorer_main(void *arg);
void app_terminal_main(void *arg);
void app_control_main(void *arg);
void app_taskman_main(void *arg);
void app_clock_main(void *arg);
void app_osk_main(void *arg);
void app_about_main(void *arg);
void app_help_main(void *arg);
void applet_display_main(void *arg);
void applet_network_main(void *arg);
void applet_input_main(void *arg);
void applet_sound_main(void *arg);
void applet_datetime_main(void *arg);
void applet_system_main(void *arg);
void app_calc_main(void *arg);
void app_cal_main(void *arg);
void app_alarm_main(void *arg);
void app_paint_main(void *arg);
void app_view_main(void *arg);
void app_notepad_main(void *arg);
void app_installer_main(void *arg);

extern const app_def_t progman_def, explorer_def, terminal_def, control_def, taskman_def, clock_def,
                       osk_def, about_def, help_def, display_def, network_def, input_def, sound_def,
                       datetime_def, system_def,
                       calc_def, cal_def, alarm_def, paint_def, view_def, notepad_def, installer_def;

#endif
