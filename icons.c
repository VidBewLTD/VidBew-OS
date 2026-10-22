/* icons.c -- original icon graphics built from our own primitives. */
#include "icons.h"
#include "font8x16.h"

/* ---------------- big icons ---------------- */

static void big_folder(surface_t *s, int x, int y, int size)
{
    s_fill(s, x + 4, y + 6, size / 2, 6, COL_YELLOW);
    s_rect(s, x + 4, y + 6, size / 2, 6, COL_BLACK);
    s_fill(s, x + 2, y + 12, size - 4, size - 18, COL_YELLOW);
    s_rect(s, x + 2, y + 12, size - 4, size - 18, COL_BLACK);
}

static void monitor_body(surface_t *s, int x, int y, int size, int *scr_x, int *scr_y, int *scr_w, int *scr_h)
{
    int bw = size - 12, bh = size - 22;
    s_fill(s, x + 6, y + 4, bw, bh, COL_DARK_GREY);
    s_rect(s, x + 6, y + 4, bw, bh, COL_BLACK);
    s_fill(s, x + 9, y + 7, bw - 6, bh - 6, COL_BLACK);
    s_fill(s, x + size / 2 - 4, y + 4 + bh, 8, 5, COL_DARK_GREY);
    s_fill(s, x + size / 2 - 10, y + 9 + bh, 20, 3, COL_DARK_GREY);
    *scr_x = x + 9; *scr_y = y + 7; *scr_w = bw - 6; *scr_h = bh - 6;
}

static void big_monitor(surface_t *s, int x, int y, int size)
{
    int sx, sy, sw, sh;
    monitor_body(s, x, y, size, &sx, &sy, &sw, &sh);
    s_fill(s, x + 11, y + 10, sw - 6, 2, COL_LIGHT_GREEN);
    s_fill(s, x + 11, y + 15, (sw - 6) / 2, 2, COL_LIGHT_GREEN);
}

static void big_display(surface_t *s, int x, int y, int size)
{
    int sx, sy, sw, sh;
    monitor_body(s, x, y, size, &sx, &sy, &sw, &sh);
    int third = sw / 3;
    s_fill(s, sx, sy, third, sh, COL_LIGHT_RED);
    s_fill(s, sx + third, sy, third, sh, COL_LIGHT_GREEN);
    s_fill(s, sx + 2 * third, sy, sw - 2 * third, sh, COL_LIGHT_BLUE);
}

static void big_keyboard(surface_t *s, int x, int y, int size)
{
    int kx = x + 3, ky = y + size / 2 - 8, kw = size - 6, kh = 20;
    s_fill(s, kx, ky, kw, kh, COL_LIGHT_GREY);
    s_rect(s, kx, ky, kw, kh, COL_BLACK);
    int cols = 7, rows = 2;
    int key_w = (kw - 6) / cols, key_h = (kh - 6) / rows;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            s_fill(s, kx + 3 + c * key_w, ky + 3 + r * key_h, key_w - 1, key_h - 1, COL_WHITE);
}

static const unsigned char pointer_mask[15][11] = {
    {1,0,0,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0,0,0},{1,1,1,0,0,0,0,0,0,0,0},
    {1,1,1,1,0,0,0,0,0,0,0},{1,1,1,1,1,0,0,0,0,0,0},{1,1,1,1,1,1,0,0,0,0,0},
    {1,1,1,1,1,1,1,0,0,0,0},{1,1,1,1,1,1,1,1,0,0,0},{1,1,1,1,1,1,1,1,1,0,0},
    {1,1,1,1,1,1,1,1,1,1,0},{1,1,1,1,1,1,1,1,1,1,1},{1,1,1,1,0,0,0,0,0,0,0},
    {1,1,1,0,0,0,0,0,0,0,0},{1,1,0,0,1,1,0,0,0,0,0},{1,0,0,0,1,1,0,0,0,0,0},
};

static void draw_pointer_shape(surface_t *s, int ox, int oy, int scale)
{
    for (int row = -1; row <= 15; row++) {
        for (int col = -1; col <= 11; col++) {
            int here = (row >= 0 && row < 15 && col >= 0 && col < 11) ? pointer_mask[row][col] : 0;
            if (here) continue;
            int touches = 0;
            for (int dy = -1; dy <= 1 && !touches; dy++)
                for (int dx = -1; dx <= 1 && !touches; dx++) {
                    int nr = row + dy, nc = col + dx;
                    if (nr >= 0 && nr < 15 && nc >= 0 && nc < 11 && pointer_mask[nr][nc]) touches = 1;
                }
            if (touches) s_fill(s, ox + col * scale, oy + row * scale, scale, scale, COL_WHITE);
        }
    }
    for (int row = 0; row < 15; row++)
        for (int col = 0; col < 11; col++)
            if (pointer_mask[row][col]) s_fill(s, ox + col * scale, oy + row * scale, scale, scale, COL_BLACK);
}

static void big_pointer(surface_t *s, int x, int y, int size)
{
    draw_pointer_shape(s, x + (size - 22) / 2, y + (size - 30) / 2, 2);
}

static void big_info(surface_t *s, int x, int y, int size)
{
    int cx = x + size / 2, cy = y + size / 2, r = size / 2 - 4;
    s_fcircle(s, cx, cy, r, COL_BLUE);
    s_circle(s, cx, cy, r, COL_BLACK);
    s_fcircle(s, cx, cy - r / 2, 2, COL_WHITE);
    s_fill(s, cx - 2, cy - 1, 4, r - 2, COL_WHITE);
}

static void big_disc(surface_t *s, int x, int y, int size)
{
    int cx = x + size / 2, cy = y + size / 2, r = size / 2 - 3;
    s_fcircle(s, cx, cy, r, COL_LIGHT_GREY);
    s_fcircle(s, cx, cy, r - 5, COL_LIGHT_CYAN);
    s_fcircle(s, cx, cy, r - 12, COL_LIGHT_GREY);
    s_fcircle(s, cx, cy, 3, COL_BLACK);
    s_circle(s, cx, cy, r, COL_BLACK);
}

static void big_control(surface_t *s, int x, int y, int size)
{
    int px = x + 4, py = y + 6, pw = size - 8, ph = size - 14;
    s_fill(s, px, py, pw, ph, COL_LIGHT_GREY);
    s_bevel1(s, px, py, pw, ph, 0);
    s_rect(s, px - 1, py - 1, pw + 2, ph + 2, COL_BLACK);
    static const int knob_col[3] = { COL_LIGHT_RED, COL_LIGHT_GREEN, COL_LIGHT_BLUE };
    static const int knob_pos[3] = { 30, 60, 15 };
    for (int i = 0; i < 3; i++) {
        int tx = px + pw * (i + 1) / 4 - 1;
        s_fill(s, tx, py + 5, 3, ph - 10, COL_DARK_GREY);
        int ky = py + 5 + (ph - 22) * knob_pos[i] / 100;
        s_fill(s, tx - 5, ky, 13, 8, knob_col[i]);
        s_rect(s, tx - 5, ky, 13, 8, COL_BLACK);
    }
}

static void big_tasks(surface_t *s, int x, int y, int size)
{
    int px = x + 4, py = y + 6, pw = size - 8, ph = size - 14;
    s_fill(s, px, py, pw, ph, COL_BLACK);
    s_rect(s, px, py, pw, ph, COL_DARK_GREY);
    static const int col[4] = { COL_LIGHT_GREEN, COL_YELLOW, COL_LIGHT_CYAN, COL_LIGHT_RED };
    static const int hgt[4] = { 40, 70, 55, 90 };
    int bw = (pw - 10) / 4;
    for (int i = 0; i < 4; i++) {
        int bh = (ph - 8) * hgt[i] / 100;
        s_fill(s, px + 4 + i * (bw + 1), py + ph - 4 - bh, bw - 1, bh, col[i]);
    }
}

static void big_clock(surface_t *s, int x, int y, int size)
{
    int cx = x + size / 2, cy = y + size / 2, r = size / 2 - 3;
    s_fcircle(s, cx, cy, r, COL_WHITE);
    s_circle(s, cx, cy, r, COL_BLACK);
    s_circle(s, cx, cy, r - 1, COL_DARK_GREY);
    s_vline(s, cx, cy - r + 3, 4, COL_BLACK);
    s_vline(s, cx, cy + r - 6, 4, COL_BLACK);
    s_hline(s, cx - r + 3, cy, 4, COL_BLACK);
    s_hline(s, cx + r - 6, cy, 4, COL_BLACK);
    s_line(s, cx, cy, cx, cy - r + 8, COL_BLACK);
    s_line(s, cx + 1, cy, cx + 1, cy - r + 8, COL_BLACK);
    s_line(s, cx, cy, cx + r - 10, cy + 3, COL_BLUE);
    s_fcircle(s, cx, cy, 2, COL_RED);
}

static void big_network(surface_t *s, int x, int y, int size)
{
    int cx = x + size / 2, cy = y + size / 2, r = size / 2 - 4;
    s_fcircle(s, cx, cy, r, COL_LIGHT_BLUE);
    s_fcircle(s, cx - r / 3, cy - r / 4, r / 3, COL_LIGHT_GREEN);
    s_fcircle(s, cx + r / 3, cy + r / 3, r / 3 - 1, COL_LIGHT_GREEN);
    s_hline(s, cx - r + 2, cy, 2 * r - 3, COL_BLUE);
    s_vline(s, cx, cy - r + 2, 2 * r - 3, COL_BLUE);
    s_circle(s, cx, cy, r, COL_BLACK);
    s_circle(s, cx, cy, r / 2, COL_BLUE);
}

static void big_system(surface_t *s, int x, int y, int size)
{
    int tw = size * 5 / 8, th = size - 8, tx = x + (size - tw) / 2, ty = y + 4;
    s_fill(s, tx, ty, tw, th, COL_LIGHT_GREY);
    s_bevel1(s, tx, ty, tw, th, 0);
    s_rect(s, tx - 1, ty - 1, tw + 2, th + 2, COL_BLACK);
    s_fill(s, tx + 5, ty + 6, tw - 10, 4, COL_DARK_GREY);
    s_fill(s, tx + 5, ty + 13, tw - 10, 2, COL_DARK_GREY);
    s_fill(s, tx + 5, ty + 18, tw - 10, 2, COL_DARK_GREY);
    s_fill(s, tx + tw - 12, ty + th - 10, 6, 5, COL_LIGHT_GREEN);
    s_rect(s, tx + tw - 12, ty + th - 10, 6, 5, COL_BLACK);
}

static void big_file(surface_t *s, int x, int y, int size)
{
    int fw = size * 5 / 8, fh = size - 8, fx = x + (size - fw) / 2, fy = y + 4;
    s_fill(s, fx, fy, fw, fh, COL_WHITE);
    s_rect(s, fx, fy, fw, fh, COL_BLACK);
    for (int i = 0; i < 5; i++) s_fill(s, fx + 4, fy + 8 + i * 5, fw - 8 - (i == 4 ? 8 : 0), 2, COL_DARK_GREY);
    s_fill(s, fx + fw - 9, fy + 1, 8, 8, COL_LIGHT_GREY);
    s_line(s, fx + fw - 9, fy + 1, fx + fw - 9, fy + 9, COL_BLACK);
    s_line(s, fx + fw - 9, fy + 9, fx + fw - 1, fy + 9, COL_BLACK);
}

static void big_logo(surface_t *s, int x, int y, int size)
{
    int m = 4, n = size - 8;
    s_fill(s, x + m, y + m, n, n, COL_BLUE);
    s_rect(s, x + m, y + m, n, n, COL_BLACK);
    s_bevel1(s, x + m + 1, y + m + 1, n - 2, n - 2, 0);
    for (int k = 0; k < 4; k++) {
        s_line(s, x + m + 7 + k, y + m + 7, x + size / 2 + k - 2, y + size - m - 8, COL_WHITE);
        s_line(s, x + size - m - 8 - k, y + m + 7, x + size / 2 - k + 1, y + size - m - 8, COL_WHITE);
    }
}

static void big_calendar(surface_t *s, int x, int y, int size)
{
    int cw = size - 10, cx = x + 5, cy = y + 6, ch = size - 12;
    s_fill(s, cx, cy, cw, ch, COL_WHITE);
    s_rect(s, cx, cy, cw, ch, COL_BLACK);
    s_fill(s, cx + 1, cy + 1, cw - 2, 9, COL_LIGHT_RED);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            s_fill(s, cx + 5 + c * ((cw - 10) / 4), cy + 15 + r * ((ch - 20) / 3), 4, 3, COL_DARK_GREY);
}

static void big_glyph(surface_t *s, int x, int y, int ch, int scale, int color)
{
    const unsigned char *g = font8x16[ch - FONT_FIRST_CHAR];
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 8; c++)
            if (g[r] & (0x80 >> c)) s_fill(s, x + c * scale, y + r * scale, scale, scale, color);
}

static void big_help(surface_t *s, int x, int y, int size)
{
    int cx = x + size / 2, cy = y + size / 2, r = size / 2 - 4;
    s_fcircle(s, cx, cy, r, COL_YELLOW);
    s_circle(s, cx, cy, r, COL_BLACK);
    big_glyph(s, cx - 8, cy - 16, '?', 2, COL_BLACK);
}

void icon_draw(surface_t *s, int x, int y, int size, icon_type_t type)
{
    switch (type) {
    case ICON_FOLDER:   big_folder(s, x, y, size); break;
    case ICON_MONITOR:  big_monitor(s, x, y, size); break;
    case ICON_KEYBOARD: big_keyboard(s, x, y, size); break;
    case ICON_POINTER:  big_pointer(s, x, y, size); break;
    case ICON_INFO:     big_info(s, x, y, size); break;
    case ICON_DISC:     big_disc(s, x, y, size); break;
    case ICON_CONTROL:  big_control(s, x, y, size); break;
    case ICON_TASKS:    big_tasks(s, x, y, size); break;
    case ICON_CLOCK:    big_clock(s, x, y, size); break;
    case ICON_NETWORK:  big_network(s, x, y, size); break;
    case ICON_DISPLAY:  big_display(s, x, y, size); break;
    case ICON_SYSTEM:   big_system(s, x, y, size); break;
    case ICON_FILE:     big_file(s, x, y, size); break;
    case ICON_LOGO:     big_logo(s, x, y, size); break;
    case ICON_CALENDAR: big_calendar(s, x, y, size); break;
    case ICON_HELP:     big_help(s, x, y, size); break;
    default: break;
    }
}

/* ---------------- 16x16 icons ---------------- */

static void small_monitor(surface_t *s, int x, int y)
{
    s_fill(s, x + 1, y + 2, 14, 10, COL_DARK_GREY);
    s_rect(s, x + 1, y + 2, 14, 10, COL_BLACK);
    s_fill(s, x + 3, y + 4, 10, 6, COL_BLACK);
    s_fill(s, x + 6, y + 12, 4, 2, COL_DARK_GREY);
    s_fill(s, x + 4, y + 14, 8, 1, COL_DARK_GREY);
}

void icon_draw_small(surface_t *s, int x, int y, icon_type_t type)
{
    switch (type) {
    case ICON_FOLDER:
        s_fill(s, x + 1, y + 3, 6, 3, COL_YELLOW);
        s_rect(s, x + 1, y + 3, 6, 3, COL_BLACK);
        s_fill(s, x + 1, y + 5, 14, 9, COL_YELLOW);
        s_rect(s, x + 1, y + 5, 14, 9, COL_BLACK);
        break;
    case ICON_MONITOR:
        small_monitor(s, x, y);
        s_fill(s, x + 4, y + 5, 5, 1, COL_LIGHT_GREEN);
        s_fill(s, x + 4, y + 7, 3, 1, COL_LIGHT_GREEN);
        break;
    case ICON_DISPLAY:
        small_monitor(s, x, y);
        s_fill(s, x + 3, y + 4, 3, 6, COL_LIGHT_RED);
        s_fill(s, x + 6, y + 4, 3, 6, COL_LIGHT_GREEN);
        s_fill(s, x + 9, y + 4, 4, 6, COL_LIGHT_BLUE);
        break;
    case ICON_KEYBOARD:
        s_fill(s, x, y + 4, 16, 9, COL_LIGHT_GREY);
        s_rect(s, x, y + 4, 16, 9, COL_BLACK);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 5; c++) s_fill(s, x + 2 + c * 3, y + 6 + r * 3, 2, 2, COL_WHITE);
        s_fill(s, x + 4, y + 11, 8, 1, COL_WHITE);
        break;
    case ICON_POINTER:
        draw_pointer_shape(s, x + 2, y, 1);
        break;
    case ICON_INFO:
        s_fcircle(s, x + 8, y + 8, 6, COL_BLUE);
        s_circle(s, x + 8, y + 8, 6, COL_BLACK);
        s_fill(s, x + 7, y + 4, 2, 2, COL_WHITE);
        s_fill(s, x + 7, y + 7, 2, 5, COL_WHITE);
        break;
    case ICON_DISC:
        s_fcircle(s, x + 8, y + 8, 6, COL_LIGHT_GREY);
        s_fcircle(s, x + 8, y + 8, 4, COL_LIGHT_CYAN);
        s_fcircle(s, x + 8, y + 8, 1, COL_BLACK);
        s_circle(s, x + 8, y + 8, 6, COL_BLACK);
        break;
    case ICON_CONTROL:
        s_fill(s, x + 1, y + 2, 14, 12, COL_LIGHT_GREY);
        s_rect(s, x + 1, y + 2, 14, 12, COL_BLACK);
        s_vline(s, x + 4, y + 4, 8, COL_DARK_GREY);
        s_vline(s, x + 8, y + 4, 8, COL_DARK_GREY);
        s_vline(s, x + 12, y + 4, 8, COL_DARK_GREY);
        s_fill(s, x + 3, y + 5, 3, 3, COL_LIGHT_RED);
        s_fill(s, x + 7, y + 9, 3, 3, COL_LIGHT_GREEN);
        s_fill(s, x + 11, y + 6, 3, 3, COL_LIGHT_BLUE);
        break;
    case ICON_TASKS:
        s_fill(s, x + 1, y + 2, 14, 12, COL_BLACK);
        s_rect(s, x + 1, y + 2, 14, 12, COL_DARK_GREY);
        s_fill(s, x + 3, y + 9, 2, 4, COL_LIGHT_GREEN);
        s_fill(s, x + 6, y + 6, 2, 7, COL_YELLOW);
        s_fill(s, x + 9, y + 8, 2, 5, COL_LIGHT_CYAN);
        s_fill(s, x + 12, y + 4, 2, 9, COL_LIGHT_RED);
        break;
    case ICON_CLOCK:
        s_fcircle(s, x + 8, y + 8, 7, COL_WHITE);
        s_circle(s, x + 8, y + 8, 7, COL_BLACK);
        s_line(s, x + 8, y + 8, x + 8, y + 3, COL_BLACK);
        s_line(s, x + 8, y + 8, x + 11, y + 9, COL_BLUE);
        break;
    case ICON_NETWORK:
        s_fcircle(s, x + 8, y + 8, 7, COL_LIGHT_BLUE);
        s_fill(s, x + 4, y + 4, 4, 3, COL_LIGHT_GREEN);
        s_fill(s, x + 9, y + 9, 4, 4, COL_LIGHT_GREEN);
        s_vline(s, x + 8, y + 2, 12, COL_BLUE);
        s_hline(s, x + 2, y + 8, 12, COL_BLUE);
        s_circle(s, x + 8, y + 8, 7, COL_BLACK);
        break;
    case ICON_SYSTEM:
        s_fill(s, x + 3, y + 1, 10, 14, COL_LIGHT_GREY);
        s_rect(s, x + 3, y + 1, 10, 14, COL_BLACK);
        s_fill(s, x + 5, y + 3, 6, 2, COL_DARK_GREY);
        s_fill(s, x + 5, y + 6, 6, 1, COL_DARK_GREY);
        s_fill(s, x + 9, y + 11, 2, 2, COL_LIGHT_GREEN);
        break;
    case ICON_FILE:
        s_fill(s, x + 3, y + 1, 10, 14, COL_WHITE);
        s_rect(s, x + 3, y + 1, 10, 14, COL_BLACK);
        s_fill(s, x + 5, y + 5, 6, 1, COL_DARK_GREY);
        s_fill(s, x + 5, y + 8, 6, 1, COL_DARK_GREY);
        s_fill(s, x + 5, y + 11, 4, 1, COL_DARK_GREY);
        break;
    case ICON_LOGO:
        s_fill(s, x + 1, y + 1, 14, 14, COL_BLUE);
        s_rect(s, x + 1, y + 1, 14, 14, COL_BLACK);
        s_line(s, x + 4, y + 4, x + 7, y + 12, COL_WHITE);
        s_line(s, x + 5, y + 4, x + 8, y + 12, COL_WHITE);
        s_line(s, x + 12, y + 4, x + 8, y + 12, COL_WHITE);
        s_line(s, x + 11, y + 4, x + 7, y + 12, COL_WHITE);
        break;
    case ICON_CALENDAR:
        s_fill(s, x + 2, y + 2, 12, 12, COL_WHITE);
        s_rect(s, x + 2, y + 2, 12, 12, COL_BLACK);
        s_fill(s, x + 3, y + 3, 10, 3, COL_LIGHT_RED);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++) s_fill(s, x + 4 + c * 3, y + 8 + r * 3, 2, 2, COL_DARK_GREY);
        break;
    case ICON_HELP:
        s_fcircle(s, x + 8, y + 8, 7, COL_YELLOW);
        s_circle(s, x + 8, y + 8, 7, COL_BLACK);
        s_char(s, x + 4, y, '?', COL_BLACK, COL_NONE);
        break;
    default: break;
    }
}
