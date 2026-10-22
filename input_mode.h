#ifndef INPUT_MODE_H
#define INPUT_MODE_H

typedef enum {
    INPUT_MODE_MOUSE = 0,
    INPUT_MODE_KEYBOARD_NAV = 1,
    INPUT_MODE_TOUCH = 2
} input_mode_t;

void input_mode_init(void);
input_mode_t input_mode_get(void);
void input_mode_set(input_mode_t m);
void input_mode_cycle(void);               /* F2: Mouse -> KeyNav -> Touch */
void input_mode_notify_mouse(void);        /* a real PS/2 mouse packet arrived */
void input_mode_notify_keyboard_nav(void); /* the keyboard just moved the pointer */
int input_mode_cursor_visible(void);       /* 0 in touch mode -- no persistent pointer needed */
const char *input_mode_name(void);

#endif
