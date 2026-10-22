/* input_mode.c -- tracks which pointing method is currently "active"
 * and what that implies for the cursor.
 *
 * Real touch hardware isn't something this environment has or can
 * emulate in any standard way, so there's no automatic touch
 * detection here -- TOUCH mode is reached via the F2 demo toggle
 * instead of a real touch controller announcing itself. Mouse and
 * keyboard-nav modes, on the other hand, really are driven by real
 * input: whichever one last produced an actual event becomes active.
 */

#include "input_mode.h"

static input_mode_t mode = INPUT_MODE_MOUSE;

void input_mode_init(void)
{
    mode = INPUT_MODE_MOUSE;
}

input_mode_t input_mode_get(void)
{
    return mode;
}

void input_mode_set(input_mode_t m)
{
    mode = m;
}

void input_mode_cycle(void)
{
    mode = (input_mode_t)((mode + 1) % 3);
}

void input_mode_notify_mouse(void)
{
    mode = INPUT_MODE_MOUSE;
}

void input_mode_notify_keyboard_nav(void)
{
    mode = INPUT_MODE_KEYBOARD_NAV;
}

int input_mode_cursor_visible(void)
{
    return mode != INPUT_MODE_TOUCH;
}

const char *input_mode_name(void)
{
    switch (mode) {
        case INPUT_MODE_MOUSE: return "Mouse";
        case INPUT_MODE_KEYBOARD_NAV: return "KeyNav";
        case INPUT_MODE_TOUCH: return "Touch";
        default: return "?";
    }
}
