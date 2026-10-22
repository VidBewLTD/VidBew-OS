#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

/* One event per PS/2 packet (or synthetic keyboard action), carrying the
 * absolute pointer position and the button state AFTER the event. Queueing
 * them means a quick press+release is never lost between UI polls. */
typedef struct { int x, y, buttons; } mouse_event_t;   /* buttons: 1 = left, 2 = right, 4 = middle */

extern volatile int mouse_x, mouse_y;

int  mouse_init(void);                       /* 1 if a real PS/2 mouse answered, 0 if none */
void mouse_handler_c(void);
int  mouse_get_event(mouse_event_t *ev);     /* 1 if an event was waiting */
void mouse_set_bounds(int w, int h);         /* screen size changed */
void mouse_set_speed(int percent);           /* 50..300, 100 = 1:1 */
int  mouse_get_speed(void);
void mouse_inject_move(int dx, int dy);      /* keyboard pointer keys */
void mouse_inject_click(void);               /* press+release at the current spot */

#endif
