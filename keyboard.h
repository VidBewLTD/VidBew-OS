#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Key codes: plain ASCII for printable keys (Enter = '\n', Backspace = 8,
 * Tab = 9, Esc = 27), values above 255 for everything else. */
#define KEY_UP     0x101
#define KEY_DOWN   0x102
#define KEY_LEFT   0x103
#define KEY_RIGHT  0x104
#define KEY_HOME   0x105
#define KEY_END    0x106
#define KEY_PGUP   0x107
#define KEY_PGDN   0x108
#define KEY_DEL    0x109
#define KEY_INS    0x10A
#define KEY_WIN    0x10B
#define KEY_F1     0x120
#define KEY_F2     0x121
#define KEY_F3     0x122
#define KEY_F4     0x123
#define KEY_F5     0x124
#define KEY_F6     0x125
#define KEY_F7     0x126
#define KEY_F8     0x127
#define KEY_F9     0x128
#define KEY_F10    0x129
#define KEY_F11    0x12A
#define KEY_F12    0x12B

#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4

typedef struct { int code; int mods; } key_event_t;

void keyboard_init(void);
int  keyboard_get_event(key_event_t *ev);   /* 1 if a key press was waiting */
void keyboard_push_char(int c);             /* inject a key (on-screen keyboard) */
int  keyboard_mods(void);

#endif
