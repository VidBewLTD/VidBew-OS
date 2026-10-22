/* keyboard.c -- PS/2 keyboard driver.
 *
 * The keyboard controller delivers "Scan Code Set 1" bytes on IRQ1. Each key
 * has a "make" code when pressed and a "break" code (make | 0x80) when
 * released; extended keys (arrows, Home, Delete...) arrive behind a 0xE0
 * prefix. We translate to key events (ASCII or KEY_* codes plus modifiers)
 * in a ring buffer the window manager drains.
 *
 * Pointer keys (accessibility): in keyboard-navigation mode the arrow keys
 * move the pointer instead (hold Shift for big steps), and Left Ctrl clicks.
 * Alt+Arrow grabs the pointer in any mode. In every other mode the arrows are
 * ordinary keys that apps receive. */

#include "keyboard.h"
#include "io.h"
#include "mouse.h"
#include "input_mode.h"

#define KBD_DATA_PORT 0x60

#define SC_ESC    0x01
#define SC_LCTRL  0x1D
#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_LALT   0x38
#define SC_CAPS   0x3A

#define CURSOR_STEP 6

static const char scancode_lower[] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8',   /* 0x00-0x09 */
    '9', '0', '-', '=', '\b','\t','q', 'w', 'e', 'r',   /* 0x0A-0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,    /* 0x14-0x1D (0x1D=LCtrl) */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 0x1E-0x27 */
    '\'','`', 0,  '\\','z', 'x', 'c', 'v', 'b', 'n',    /* 0x28-0x31 (0x2A=LShift) */
    'm', ',', '.', '/', 0,  '*', 0,  ' '                /* 0x32-0x39 (0x36=RShift, 0x39=space) */
};

static const char scancode_upper[] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b','\t','Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0,  '*', 0,  ' '
};

#define SCANCODE_TABLE_LEN (sizeof(scancode_lower) / sizeof(scancode_lower[0]))

static volatile int shift_l = 0, shift_r = 0, ctrl_l = 0, ctrl_r = 0, alt_l = 0, alt_r = 0, caps = 0;
static volatile int extended_pending = 0;

#define KQ_SIZE 64
static key_event_t kq[KQ_SIZE];
static volatile int kq_head = 0, kq_tail = 0;

int keyboard_mods(void)
{
    int m = 0;
    if (shift_l || shift_r) m |= MOD_SHIFT;
    if (ctrl_l || ctrl_r) m |= MOD_CTRL;
    if (alt_l || alt_r) m |= MOD_ALT;
    return m;
}

static void kq_push(int code, int mods)
{
    int next = (kq_head + 1) % KQ_SIZE;
    if (next == kq_tail) return;
    kq[kq_head].code = code;
    kq[kq_head].mods = mods;
    kq_head = next;
}

void keyboard_push_char(int c)
{
    kq_push(c, 0);
}

int keyboard_get_event(key_event_t *ev)
{
    if (kq_head == kq_tail) return 0;
    *ev = kq[kq_tail];
    kq_tail = (kq_tail + 1) % KQ_SIZE;
    return 1;
}

static int pointer_keys_active(void)
{
    return input_mode_get() == INPUT_MODE_KEYBOARD_NAV || (alt_l || alt_r);
}

/* Called from irq1_stub (isr.asm) on every keyboard interrupt. */
void keyboard_handler_c(void)
{
    unsigned char scancode = inb(KBD_DATA_PORT);

    if (scancode == 0xE0) {
        extended_pending = 1;
        outb(0x20, 0x20);
        return;
    }

    int released = scancode & 0x80;
    unsigned char code = scancode & 0x7F;

    if (extended_pending) {
        extended_pending = 0;
        switch (code) {
        case 0x1D: ctrl_r = !released; break;
        case 0x38: alt_r = !released; break;
        default:
            if (!released) {
                int step = (shift_l || shift_r) ? CURSOR_STEP * 4 : CURSOR_STEP;
                int arrow = (code == 0x48 || code == 0x50 || code == 0x4B || code == 0x4D);
                if (arrow && pointer_keys_active()) {
                    int dx = 0, dy = 0;
                    if (code == 0x48) dy = -step;
                    if (code == 0x50) dy = step;
                    if (code == 0x4B) dx = -step;
                    if (code == 0x4D) dx = step;
                    mouse_inject_move(dx, dy);
                    input_mode_notify_keyboard_nav();
                } else {
                    int k = 0;
                    switch (code) {
                    case 0x48: k = KEY_UP; break;
                    case 0x50: k = KEY_DOWN; break;
                    case 0x4B: k = KEY_LEFT; break;
                    case 0x4D: k = KEY_RIGHT; break;
                    case 0x47: k = KEY_HOME; break;
                    case 0x4F: k = KEY_END; break;
                    case 0x49: k = KEY_PGUP; break;
                    case 0x51: k = KEY_PGDN; break;
                    case 0x53: k = KEY_DEL; break;
                    case 0x52: k = KEY_INS; break;
                    case 0x5B: case 0x5C: k = KEY_WIN; break;
                    case 0x1C: k = '\n'; break;   /* keypad enter */
                    case 0x35: k = '/'; break;    /* keypad slash */
                    default: break;
                    }
                    if (k) kq_push(k, keyboard_mods());
                }
            }
            break;
        }
        outb(0x20, 0x20);
        return;
    }

    switch (code) {
    case SC_LSHIFT: shift_l = !released; break;
    case SC_RSHIFT: shift_r = !released; break;
    case SC_LCTRL:
        ctrl_l = !released;
        if (!released && (input_mode_get() == INPUT_MODE_KEYBOARD_NAV || input_mode_get() == INPUT_MODE_TOUCH))
            mouse_inject_click();
        break;
    case SC_LALT: alt_l = !released; break;
    case SC_CAPS: if (!released) caps = !caps; break;
    default:
        if (!released) {
            int k = 0;
            if (code >= 0x3B && code <= 0x44) k = KEY_F1 + (code - 0x3B);
            else if (code == 0x57) k = KEY_F11;
            else if (code == 0x58) k = KEY_F12;
            else if (code < SCANCODE_TABLE_LEN) {
                int shifted = (shift_l || shift_r);
                char base = scancode_lower[code];
                if (base >= 'a' && base <= 'z') shifted ^= caps;
                char c = shifted ? scancode_upper[code] : scancode_lower[code];
                if (c) k = (unsigned char)c;
            }
            if (k) kq_push(k, keyboard_mods());
        }
        break;
    }

    outb(0x20, 0x20); /* EOI to master PIC */
}

void keyboard_init(void)
{
    kq_head = kq_tail = 0;
    shift_l = shift_r = ctrl_l = ctrl_r = alt_l = alt_r = caps = 0;
    extended_pending = 0;
}
