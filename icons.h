#ifndef ICONS_H
#define ICONS_H

#include "gfx.h"

typedef enum {
    ICON_FOLDER, ICON_MONITOR, ICON_KEYBOARD, ICON_POINTER, ICON_INFO, ICON_DISC,
    ICON_CONTROL, ICON_TASKS, ICON_CLOCK, ICON_NETWORK, ICON_DISPLAY, ICON_SYSTEM,
    ICON_FILE, ICON_LOGO, ICON_CALENDAR, ICON_HELP,
    ICON_COUNT
} icon_type_t;

/* Original icons composed from our own rect/circle primitives -- no external
 * assets. icon_draw scales with `size` (about 40-56 looks best);
 * icon_draw_small is a hand-tuned 16x16 version for title bars, the taskbar
 * and menus. */
void icon_draw(surface_t *s, int x, int y, int size, icon_type_t type);
void icon_draw_small(surface_t *s, int x, int y, icon_type_t type);

#endif
