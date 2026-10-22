/* app_osk.c -- the on-screen keyboard: a topmost window that never takes
 * keyboard focus, so a click on a key types into whatever window you were
 * using. For touch screens or when no physical keyboard is handy. */

#include "apps.h"
#include "ui.h"
#include "keyboard.h"
#include "kstd.h"

const app_def_t osk_def = { "Keyboard", ICON_KEYBOARD, { 0, 0 }, { 0, 0 }, { 0, 0 } };

#define UNIT 34             /* one key = 4 quarter-units */
#define KEY_H 32
#define SHIFT_KEY (-1)

typedef struct { const char *lbl; int code; int shifted; int q; } okey_t;

#define K(l, c, s, q) { l, c, s, q }
#define CH(c, s) { 0, c, s, 4 }

static const okey_t row0[] = {
    CH('1','!'), CH('2','@'), CH('3','#'), CH('4','$'), CH('5','%'), CH('6','^'), CH('7','&'),
    CH('8','*'), CH('9','('), CH('0',')'), CH('-','_'), CH('=','+'), K("Bksp", 8, 8, 8), { 0, 0, 0, 0 } };
static const okey_t row1[] = {
    K("Tab", 9, 9, 6), CH('q','Q'), CH('w','W'), CH('e','E'), CH('r','R'), CH('t','T'), CH('y','Y'),
    CH('u','U'), CH('i','I'), CH('o','O'), CH('p','P'), CH('[','{'), CH(']','}'), K("\\", '\\', '|', 6), { 0, 0, 0, 0 } };
static const okey_t row2[] = {
    K("Esc", 27, 27, 7), CH('a','A'), CH('s','S'), CH('d','D'), CH('f','F'), CH('g','G'), CH('h','H'),
    CH('j','J'), CH('k','K'), CH('l','L'), CH(';',':'), CH('\'','"'), K("Enter", '\n', '\n', 9), { 0, 0, 0, 0 } };
static const okey_t row3[] = {
    K("Shift", SHIFT_KEY, SHIFT_KEY, 9), CH('z','Z'), CH('x','X'), CH('c','C'), CH('v','V'), CH('b','B'),
    CH('n','N'), CH('m','M'), CH(',','<'), CH('.','>'), CH('/','?'), K("Shift", SHIFT_KEY, SHIFT_KEY, 11), { 0, 0, 0, 0 } };
static const okey_t row4[] = {
    K("Del", KEY_DEL, KEY_DEL, 8), K("<", KEY_LEFT, KEY_LEFT, 4), K("^", KEY_UP, KEY_UP, 4),
    K("v", KEY_DOWN, KEY_DOWN, 4), K(">", KEY_RIGHT, KEY_RIGHT, 4), K("Space", ' ', ' ', 36), { 0, 0, 0, 0 } };

static const okey_t *rows[5] = { row0, row1, row2, row3, row4 };

typedef struct { int row, idx; } keyref_t;

static int hit_key(int mx, int my, keyref_t *out)
{
    for (int r = 0; r < 5; r++) {
        int y = 4 + r * (KEY_H + 2);
        if (my < y || my >= y + KEY_H) continue;
        int x = 4;
        for (int i = 0; rows[r][i].q; i++) {
            int w = rows[r][i].q * UNIT / 4;
            if (mx >= x && mx < x + w - 2) { out->row = r; out->idx = i; return 1; }
            x += w;
        }
    }
    return 0;
}

static void paint(window_t *win, int shift, const keyref_t *down)
{
    surface_t *s = wm_surface(win);
    int cw, ch;
    wm_client_size(win, &cw, &ch);
    s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
    for (int r = 0; r < 5; r++) {
        int y = 4 + r * (KEY_H + 2), x = 4;
        for (int i = 0; rows[r][i].q; i++) {
            const okey_t *k = &rows[r][i];
            int w = k->q * UNIT / 4;
            char lbl[2] = { 0, 0 };
            const char *text = k->lbl;
            if (!text) { lbl[0] = (char)(shift ? k->shifted : k->code); text = lbl; }
            int pressed = (down && down->row == r && down->idx == i) || (k->code == SHIFT_KEY && shift);
            ui_button(s, x, y, w - 2, KEY_H, text, pressed, 0);
            x += w;
        }
    }
    wm_invalidate(win);
}

void app_osk_main(void *arg)
{
    int cw = 15 * UNIT + 6, ch = 5 * (KEY_H + 2) + 6;
    window_t *win = wm_create("Keyboard", cw, ch, WF_TOPMOST | WF_NO_FOCUS | WF_NO_MAX | WF_NO_TASKBAR, &osk_def);
    if (!win) return;
    wm_dock_bottom(win);
    int shift = 0, have_down = 0;
    keyref_t down = { 0, 0 };
    paint(win, shift, 0);

    event_t ev;
    for (;;) {
        wm_next_event(win, &ev, -1);
        if (ev.type == EV_CLOSE) { wm_destroy(win); return; }
        if (ev.type == EV_MOUSE_DOWN && ev.c == 1) {
            keyref_t k;
            if (hit_key(ev.a, ev.b, &k)) {
                const okey_t *key = &rows[k.row][k.idx];
                if (key->code == SHIFT_KEY) shift = !shift;
                else {
                    wm_post_key(shift ? key->shifted : key->code, 0);
                    if (shift && key->lbl == 0) shift = 0;       /* shift applies to one key */
                }
                down = k;
                have_down = 1;
            }
        } else if (ev.type == EV_MOUSE_UP) {
            have_down = 0;
        } else if (ev.type != EV_REPAINT && ev.type != EV_FOCUS && ev.type != EV_BLUR && ev.type != EV_MOUSE_MOVE) {
            continue;
        }
        paint(win, shift, have_down ? &down : 0);
    }
}
