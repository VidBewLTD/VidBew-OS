/* app_tools.c -- Calculator, Calendar, Alarm, Paint and Image Viewer. */

#include "apps.h"
#include "ui.h"
#include "kstd.h"
#include "heap.h"
#include "rtc.h"
#include "timer.h"
#include "keyboard.h"
#include "fat16.h"
#include "audio.h"

const app_def_t calc_def    = { "Calculator",   ICON_CONTROL,  { 0, 0 }, { 0, 0 }, { 0, 0 } };
const app_def_t cal_def     = { "Calendar",     ICON_CALENDAR, { 0, 0 }, { 0, 0 }, { 0, 0 } };
const app_def_t alarm_def   = { "Alarm",        ICON_CLOCK,    { 0, 0 }, { 0, 0 }, { 0, 0 } };
const app_def_t paint_def   = { "Paint",        ICON_DISC,     { 0, 0 }, { 0, 0 }, { 0, 0 } };
const app_def_t view_def    = { "Image Viewer", ICON_DISPLAY,  { 0, 0 }, { 0, 0 }, { 0, 0 } };
const app_def_t notepad_def = { "Text Editor",  ICON_FILE,     { 0, 0 }, { 0, 0 }, { 0, 0 } };

/* ================================================================ BMP ==== */

typedef struct { int w, h; u8 *px; } image_t;

static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static int rd16(const u8 *p) { return p[0] | (p[1] << 8); }

/* RGB -> palette index. The 16 base colours match exactly (so a Paint save/open
 * round trip is lossless and white stays white); everything else uses the cube/ramp. */
static u8 map_rgb(int r, int g, int b)
{
    for (int i = 0; i < 16; i++) {
        int pr, pg, pb;
        gfx_palette_rgb(i, &pr, &pg, &pb);
        if (pr == r && pg == g && pb == b) return (u8)i;
    }
    if (r == g && g == b) { if (r < 4) return COL_BLACK; if (r > 250) return COL_WHITE; }
    return gfx_rgb(r, g, b);
}

/* Decode an uncompressed 1/4/8/24/32-bit BMP into palette indices. 0 = ok. */
static int bmp_decode(const u8 *d, u32 n, image_t *out)
{
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return 1;
    u32 off = rd32(d + 10), hdr = rd32(d + 14);
    if (hdr < 40) return 2;
    int w = (int)rd32(d + 18), h = (int)rd32(d + 22), bpp = rd16(d + 28);
    u32 comp = rd32(d + 30), ncol = rd32(d + 46);
    int flip = 1;
    if (h < 0) { h = -h; flip = 0; }
    if (comp != 0 && !(comp == 3 && (bpp == 32 || bpp == 16))) return 3;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return 3;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return 4;
    u32 stride = (((u32)w * bpp + 31) / 32) * 4;
    if (off + stride * (u32)h > n) return 5;
    const u8 *pal = d + 14 + hdr;
    if (bpp <= 8 && ncol == 0) ncol = 1u << bpp;
    u8 *px = kmalloc((u32)w * h);
    if (!px) return 6;
    for (int y = 0; y < h; y++) {
        const u8 *row = d + off + stride * (u32)(flip ? h - 1 - y : y);
        u8 *dst = px + y * w;
        for (int x = 0; x < w; x++) {
            int r, g, b;
            if (bpp == 24) { b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; }
            else if (bpp == 32) { b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; }
            else {
                int idx;
                if (bpp == 8) idx = row[x];
                else if (bpp == 4) idx = (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
                else idx = (row[x >> 3] >> (7 - (x & 7))) & 1;
                if ((u32)idx >= ncol) idx = 0;
                b = pal[idx * 4]; g = pal[idx * 4 + 1]; r = pal[idx * 4 + 2];
            }
            dst[x] = map_rgb(r, g, b);
        }
    }
    out->w = w; out->h = h; out->px = px;
    return 0;
}

static void put32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* Encode palette indices as a 24-bit BMP; returns buffer (kfree it) or 0. */
static u8 *bmp_encode(const u8 *px, int w, int h, u32 *out_size)
{
    u32 stride = (((u32)w * 24 + 31) / 32) * 4, total = 54 + stride * (u32)h;
    u8 *b = kzalloc(total);
    if (!b) return 0;
    b[0] = 'B'; b[1] = 'M';
    put32(b + 2, total); put32(b + 10, 54); put32(b + 14, 40);
    put32(b + 18, (u32)w); put32(b + 22, (u32)h);
    b[26] = 1; b[28] = 24;
    put32(b + 34, stride * (u32)h);
    for (int y = 0; y < h; y++) {
        u8 *row = b + 54 + stride * (u32)(h - 1 - y);
        for (int x = 0; x < w; x++) {
            int r, g, bl;
            gfx_palette_rgb(px[y * w + x], &r, &g, &bl);
            row[x * 3] = bl; row[x * 3 + 1] = g; row[x * 3 + 2] = r;
        }
    }
    *out_size = total;
    return b;
}

static int load_bmp_file(const char *name, image_t *img)
{
    fat16_entry_t ents[64];
    int n = fat16_read_dir(ents, 64);
    u32 size = 0;
    for (int i = 0; i < n; i++) if (kstricmp(ents[i].name, name) == 0) size = ents[i].size;
    if (!size) return 1;
    u8 *buf = kmalloc(size);
    if (!buf) return 6;
    u32 got = 0;
    int r = fat16_read_file(name, buf, size, &got);
    int err = r ? bmp_decode(buf, got, img) : 1;     /* fat16_read_file: 1 = success */
    kfree(buf);
    return err;
}

/* ============================================================ Calculator === */

#define FP 100   /* two decimal places, kept in a plain 32-bit int */

static int fp_ovf;   /* set when a result would not fit */
static int fp_mul(int a, int b)
{
    long long chk = (long long)(a < 0 ? -a : a) * (long long)(b < 0 ? -b : b);
    if (chk > 99999999900LL) { fp_ovf = 1; return 0; }
    return (a / FP) * b + ((a % FP) * b) / FP;
}
static int fp_div(int a, int b) { return b ? (a / b) * FP + ((a % b) * FP) / b : 0; }

static void fp_str(int v, char *out, int cap)
{
    int neg = v < 0; if (neg) v = -v;
    if (v % FP) ksnprintf(out, cap, "%s%d.%02d", neg ? "-" : "", v / FP, v % FP);
    else ksnprintf(out, cap, "%s%d", neg ? "-" : "", v / FP);
}

void app_calc_main(void *arg)
{
    static const char *keys[5][4] = {
        { "C", "+/-", "%", "/" }, { "7", "8", "9", "*" }, { "4", "5", "6", "-" },
        { "1", "2", "3", "+" },   { "0", ".", "<", "=" }
    };
    window_t *win = wm_create("Calculator", 200, 270, WF_NO_MAX, &calc_def);
    if (!win) return;
    surface_t *s = wm_surface(win);
    int acc = 0, cur = 0, op = 0, fresh = 1, dec = 0, err = 0, pr = -1, pc = -1;
    for (;;) {
        s_fill(s, 0, 0, 200, 270, COL_LIGHT_GREY);
        char t[24];
        if (err) kstrlcpy(t, "Error", sizeof(t)); else fp_str(cur, t, sizeof(t));
        ui_panel(s, 8, 8, 184, 36, 1);
        s_text(s, 184 - s_text_width(t), 18, t, COL_BLACK, COL_NONE);
        for (int r = 0; r < 5; r++) for (int c = 0; c < 4; c++)
            ui_button(s, 8 + c * 46, 54 + r * 42, 42, 38, keys[r][c], pr == r && pc == c, 0);
        wm_invalidate(win);

        event_t ev;
        wm_next_event(win, &ev, -1);
        if (ev.type == EV_CLOSE) { wm_destroy(win); return; }
        const char *k = 0;
        int nr = -1, nc = -1;
        if (ev.type == EV_MOUSE_DOWN && ev.c == 1) {
            for (int r = 0; r < 5; r++) for (int c = 0; c < 4; c++)
                if (ui_inside(ev.a, ev.b, 8 + c * 46, 54 + r * 42, 42, 38)) { nr = r; nc = c; }
            pr = nr; pc = nc;
            continue;
        }
        if (ev.type == EV_MOUSE_UP) {
            if (pr >= 0 && ui_inside(ev.a, ev.b, 8 + pc * 46, 54 + pr * 42, 42, 38)) k = keys[pr][pc];
            pr = pc = -1;
        } else if (ev.type == EV_KEY) {
            static char one[2];
            int c = ev.a;
            if (c == '\n') c = '=';
            if (c == 8) c = '<';
            if (c == 27 || c == 'c') c = 'C';
            if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '*' || c == '/' ||
                c == '=' || c == '<' || c == 'C' || c == '%') { one[0] = (char)c; one[1] = 0; k = one; }
        }
        if (!k) continue;
        char c = k[0];
        if (err && c != 'C') continue;
        if ((c >= '0' && c <= '9')) {
            if (fresh) { cur = 0; dec = 0; fresh = 0; }
            int d = c - '0';
            if (dec == 0) { if (cur < 9999999 * FP / 10) cur = (cur / FP * 10 + d) * FP; }
            else if (dec == 1) { cur += d * 10; dec = 2; }
            else if (dec == 2) { cur += d; dec = 3; }
            if (cur < 0) cur = 0;
        } else if (c == '.') { if (fresh) { cur = 0; fresh = 0; } if (!dec) dec = 1; }
        else if (c == 'C') { acc = cur = 0; op = 0; fresh = 1; dec = 0; err = 0; }
        else if (k[0] == '+' && k[1] == '/') cur = -cur;
        else if (c == '%') cur = cur / 100;
        else if (c == '<') { if (dec == 3) { cur -= cur % 10; dec = 2; } else if (dec == 2) { cur -= cur % FP; dec = 1; }
                              else if (dec == 1) dec = 0; else cur = (cur / FP / 10) * FP; }
        else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=') {
            if (op && !fresh) {
                if (op == '+') acc += cur; else if (op == '-') acc -= cur;
                else if (op == '*') { fp_ovf = 0; acc = fp_mul(acc, cur); if (fp_ovf) err = 1; }
                else if (cur == 0) err = 1; else acc = fp_div(acc, cur);
            } else if (!op || fresh) { if (!op) acc = cur; }
            cur = acc; fresh = 1; dec = 0;
            op = (c == '=') ? 0 : c;
        }
    }
}

/* ========================================================== Text Editor === */

#define NOTE_CAP 2048

void app_notepad_main(void *arg)
{
    window_t *win = wm_create("Text Editor - NOTE.TXT", 380, 260, WF_RESIZABLE, &notepad_def);
    if (!win) return;
    wm_set_min_size(win, 260, 160);

    const char *fname = "NOTE.TXT";
    if (arg && *(const char *)arg) fname = (const char *)arg;

    char *buf = (char *)kmalloc(NOTE_CAP);
    if (!buf) { wm_destroy(win); return; }
    memset(buf, 0, NOTE_CAP);

    u32 size = 0;
    if (fat16_read_file(fname, (u8 *)buf, NOTE_CAP - 1, &size)) {
        buf[size] = 0;
    } else {
        kstrlcpy(buf, "Welcome to VidBew Text Editor!\nType text here and click Save.\n", NOTE_CAP);
    }

    char status[48] = "Editing...";
    for (;;) {
        surface_t *s = wm_surface(win);
        int cw, ch;
        wm_client_size(win, &cw, &ch);

        if (s->w >= cw && s->h >= ch) {
            s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);

            /* Toolbar */
            ui_button(s, 6, 6, 56, 22, "Save", 0, 0);
            ui_button(s, 68, 6, 56, 22, "Clear", 0, 0);
            s_text(s, 132, 10, fname, COL_BLACK, COL_NONE);
            s_text(s, cw - s_text_width(status) - 10, 10, status, COL_DARK_GREY, COL_NONE);

            /* Text Canvas Area */
            int tx = 6, ty = 34, tw = cw - 12, th = ch - 40;
            s_bevel(s, tx - 1, ty - 1, tw + 2, th + 2, 1);
            s_fill(s, tx, ty, tw, th, COL_WHITE);

            /* Draw text buffer */
            surface_t area = *s;
            surf_set_clip(&area, tx, ty, tw, th);

            int cx = tx + 4, cy = ty + 4;
            int len = (int)kstrlen(buf);
            for (int i = 0; i < len; i++) {
                char c = buf[i];
                if (c == '\n') {
                    cx = tx + 4;
                    cy += FONT_HEIGHT;
                } else {
                    if (cx + FONT_WIDTH > tx + tw - 4) {
                        cx = tx + 4;
                        cy += FONT_HEIGHT;
                    }
                    s_char(&area, cx, cy, c, COL_BLACK, COL_NONE);
                    cx += FONT_WIDTH;
                }
            }

            /* Draw Cursor */
            if (wm_is_focused(win)) {
                s_fill(&area, cx, cy, 2, FONT_HEIGHT, COL_BLACK);
            }

            surf_reset_clip(&area);
            wm_invalidate(win);
        }

        event_t ev;
        wm_next_event(win, &ev, -1);
        if (ev.type == EV_CLOSE) { kfree(buf); wm_destroy(win); return; }
        if (ev.type == EV_RESIZE) wm_resize_client(win, ev.a, ev.b);

        if (ev.type == EV_MOUSE_DOWN && ev.c == 1) {
            if (ui_inside(ev.a, ev.b, 6, 6, 56, 22)) {
                int ok = fat16_write_file(fname, (const u8 *)buf, kstrlen(buf));
                kstrlcpy(status, ok ? "Saved!" : "Error saving!", sizeof(status));
            } else if (ui_inside(ev.a, ev.b, 68, 6, 56, 22)) {
                buf[0] = 0;
                kstrlcpy(status, "Cleared", sizeof(status));
            }
        }

        if (ev.type == EV_KEY) {
            int code = ev.a;
            int len = (int)kstrlen(buf);
            if (code == '\b') {
                if (len > 0) buf[len - 1] = 0;
                kstrlcpy(status, "Editing...", sizeof(status));
            } else if (code == '\n') {
                if (len + 1 < NOTE_CAP - 1) {
                    buf[len] = '\n';
                    buf[len + 1] = 0;
                }
                kstrlcpy(status, "Editing...", sizeof(status));
            } else if (code >= 32 && code <= 126) {
                if (len + 1 < NOTE_CAP - 1) {
                    buf[len] = (char)code;
                    buf[len + 1] = 0;
                }
                kstrlcpy(status, "Editing...", sizeof(status));
            }
        }
    }
}

/* ============================================================== Calendar === */

static int dow(int y, int m, int d)   /* 0 = Sunday */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void app_cal_main(void *arg)
{
    static const char *mn[12] = { "January", "February", "March", "April", "May", "June", "July",
                                  "August", "September", "October", "November", "December" };
    window_t *win = wm_create("Calendar", 266, 260, WF_NO_MAX, &cal_def);
    if (!win) return;
    surface_t *s = wm_surface(win);
    int ty, tm, td, h, mi, se;
    rtc_read_date(&ty, &tm, &td);
    rtc_read_time(&h, &mi, &se);
    int y = ty, m = tm;
    for (;;) {
        s_fill(s, 0, 0, 266, 260, COL_LIGHT_GREY);
        ui_button(s, 8, 8, 28, 24, "<", 0, 0);
        ui_button(s, 230, 8, 28, 24, ">", 0, 0);
        char t[40];
        ksnprintf(t, sizeof(t), "%s %d", mn[m - 1], y);
        ui_text_center(s, 40, 12, 186, t, COL_BLACK);
        static const char *dn[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
        for (int i = 0; i < 7; i++) s_text(s, 12 + i * 36 + 8, 44, dn[i], COL_BLUE, COL_NONE);
        s_hline(s, 8, 62, 250, COL_DARK_GREY);
        int first = dow(y, m, 1), nd = rtc_days_in_month(y, m);
        for (int d = 1; d <= nd; d++) {
            int cell = first + d - 1, cx = 8 + (cell % 7) * 36, cy = 68 + (cell / 7) * 28;
            if (y == ty && m == tm && d == td) { s_fill(s, cx, cy, 34, 24, COL_BLUE); }
            else s_rect(s, cx, cy, 34, 24, COL_DARK_GREY);
            char ds[4]; ksnprintf(ds, sizeof(ds), "%d", d);
            ui_text_center(s, cx, cy + 4, 34, ds, (y == ty && m == tm && d == td) ? COL_WHITE : COL_BLACK);
        }
        wm_invalidate(win);
        event_t ev;
        wm_next_event(win, &ev, -1);
        if (ev.type == EV_CLOSE) { wm_destroy(win); return; }
        int dm = 0;
        if (ev.type == EV_MOUSE_UP) {
            if (ui_inside(ev.a, ev.b, 8, 8, 28, 24)) dm = -1;
            if (ui_inside(ev.a, ev.b, 230, 8, 28, 24)) dm = 1;
        }
        if (ev.type == EV_KEY) { if (ev.a == KEY_LEFT || ev.a == KEY_PGUP) dm = -1; if (ev.a == KEY_RIGHT || ev.a == KEY_PGDN) dm = 1; }
        m += dm;
        if (m < 1) { m = 12; y--; }
        if (m > 12) { m = 1; y++; }
    }
}

/* ================================================================ Alarm === */

#define NALARM 4
static struct { int h, m, on; } alarms[NALARM] = { { 7, 0, 0 }, { 12, 0, 0 }, { 18, 30, 0 }, { 22, 0, 0 } };

void app_alarm_main(void *arg)
{
    window_t *win = wm_create("Alarm", 300, 220, WF_NO_MAX, &alarm_def);
    if (!win) return;
    surface_t *s = wm_surface(win);
    int last_h, last_m, last_s;
    rtc_read_time(&last_h, &last_m, &last_s);   /* only a *new* minute can ring an alarm */
    for (;;) {
        int h, m, sec;
        rtc_read_time(&h, &m, &sec);
        s_fill(s, 0, 0, 300, 220, COL_LIGHT_GREY);
        char t[40];
        ksnprintf(t, sizeof(t), "Now %02d:%02d:%02d", h, m, sec);
        s_text(s, 12, 8, t, COL_DARK_GREY, COL_NONE);
        for (int i = 0; i < NALARM; i++) {
            int y = 34 + i * 40;
            ui_checkbox(s, 12, y + 4, "", alarms[i].on, 0);
            ksnprintf(t, sizeof(t), "%02d:%02d", alarms[i].h, alarms[i].m);
            s_text(s, 44, y + 6, t, COL_BLACK, COL_NONE);
            ui_button(s, 110, y, 26, 26, "h+", 0, 0); ui_button(s, 140, y, 26, 26, "h-", 0, 0);
            ui_button(s, 178, y, 26, 26, "m+", 0, 0); ui_button(s, 208, y, 26, 26, "m-", 0, 0);
        }
        s_text(s, 12, 198, "Keep this window open (minimize it) to ring.", COL_DARK_GREY, COL_NONE);
        wm_invalidate(win);
        if (h != last_h || m != last_m) {
            last_h = h; last_m = m;
            for (int i = 0; i < NALARM; i++) {
                if (!alarms[i].on || alarms[i].h != h || alarms[i].m != m) continue;
                char msg[40];
                ksnprintf(msg, sizeof(msg), "Alarm  %02d:%02d", h, m);
                dlg_notice("Alarm", msg);        /* its own task: this window keeps answering */
                audio_play_file("CHIME.WAV", 0, 0);
                break;
            }
        }
        event_t ev;
        if (!wm_next_event(win, &ev, 50)) continue;
        if (ev.type == EV_CLOSE) { wm_destroy(win); return; }
        if (ev.type == EV_MOUSE_UP) for (int i = 0; i < NALARM; i++) {
            int y = 34 + i * 40;
            if (ui_inside(ev.a, ev.b, 12, y + 4, 24, 20)) alarms[i].on = !alarms[i].on;
            if (ui_inside(ev.a, ev.b, 110, y, 26, 26)) alarms[i].h = (alarms[i].h + 1) % 24;
            if (ui_inside(ev.a, ev.b, 140, y, 26, 26)) alarms[i].h = (alarms[i].h + 23) % 24;
            if (ui_inside(ev.a, ev.b, 178, y, 26, 26)) alarms[i].m = (alarms[i].m + 1) % 60;
            if (ui_inside(ev.a, ev.b, 208, y, 26, 26)) alarms[i].m = (alarms[i].m + 59) % 60;
        }
    }
}

/* ================================================================ Paint === */

#define CW 360
#define CH 240
enum { T_PEN, T_LINE, T_RECT, T_FRECT, T_ELL, T_FILL, T_ERASE, T_COUNT };
static const char *tool_names[T_COUNT] = { "Pen", "Line", "Rect", "Fill R", "Oval", "Fill", "Erase" };

static void paint_flood(u8 *px, int w, int h, int x, int y, int col)
{
    int target = px[y * w + x];
    if (target == col) return;
    int cap = 4096, sp = 0;
    int *st = kmalloc(cap * sizeof(int));
    if (!st) return;
    st[sp++] = y * w + x;
    while (sp) {
        int p = st[--sp], px_ = p % w, py = p / w;
        if (px[p] != target) continue;
        px[p] = (u8)col;
        int nb[4][2] = { { px_ - 1, py }, { px_ + 1, py }, { px_, py - 1 }, { px_, py + 1 } };
        for (int i = 0; i < 4; i++) {
            if (nb[i][0] < 0 || nb[i][1] < 0 || nb[i][0] >= w || nb[i][1] >= h) continue;
            if (sp + 1 >= cap) {
                int *n2 = kmalloc(cap * 2 * sizeof(int));
                if (!n2) { kfree(st); return; }
                memcpy(n2, st, sp * sizeof(int)); kfree(st); st = n2; cap *= 2;
            }
            if (px[nb[i][1] * w + nb[i][0]] == target) st[sp++] = nb[i][1] * w + nb[i][0];
        }
    }
    kfree(st);
}

static int sin5(int a)   /* sine in 1/1000ths, a in 5-degree steps */
{
    static const int sn[19] = { 0, 87, 174, 259, 342, 423, 500, 574, 643, 707, 766, 819, 866, 906, 940, 966, 985, 996, 1000 };
    a = ((a % 72) + 72) % 72;
    int q = a / 18, r = a % 18;
    int v = (q & 1) ? sn[18 - r] : sn[r];
    return q >= 2 ? -v : v;
}

static void shape(surface_t *c, int tool, int x0, int y0, int x1, int y1, int col)
{
    int lx = x0 < x1 ? x0 : x1, ly = y0 < y1 ? y0 : y1, w = (x0 < x1 ? x1 - x0 : x0 - x1) + 1, h = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
    if (tool == T_LINE) s_line(c, x0, y0, x1, y1, col);
    else if (tool == T_RECT) s_rect(c, lx, ly, w, h, col);
    else if (tool == T_FRECT) s_fill(c, lx, ly, w, h, col);
    else if (tool == T_ELL) {   /* 72-segment polygon from a 5-degree sine table */
        int rx = w / 2, ry = h / 2, cx = lx + rx, cy = ly + ry;
        int px_ = cx + rx, py = cy;
        for (int a = 1; a <= 72; a++) {
            int sv = sin5(a), cv = sin5(a + 18);
            int nx = cx + rx * cv / 1000, ny = cy + ry * sv / 1000;
            s_line(c, px_, py, nx, ny, col);
            px_ = nx; py = ny;
        }
    }
}

void app_paint_main(void *arg)
{
    int cw = CW + 64, ch = CH + 72;
    window_t *win = wm_create("Paint - untitled", cw, ch, WF_NO_MAX, &paint_def);
    if (!win) return;
    surface_t *s = wm_surface(win);
    u8 *buf = kmalloc(CW * CH);
    if (!buf) { wm_destroy(win); return; }
    memset(buf, COL_WHITE, CW * CH);
    surface_t canvas, preview;
    surf_init(&canvas, CW, CH, buf);
    (void)preview;
    int tool = T_PEN, col = COL_BLACK, size = 1, drag = 0, sx = 0, sy = 0, ex = 0, ey = 0;
    char status[48] = "Ready";
    const int ox = 60, oy = 6;
    for (;;) {
        s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
        for (int i = 0; i < T_COUNT; i++) ui_button(s, 4, 6 + i * 26, 52, 24, tool_names[i], tool == i, 0);
        ui_button(s, 4, 196, 24, 22, "-", 0, 0); ui_button(s, 32, 196, 24, 22, "+", 0, 0);
        char t[16]; ksnprintf(t, sizeof(t), "Size %d", size); s_text(s, 4, 220, t, COL_BLACK, COL_NONE);
        for (int i = 0; i < 16; i++) {
            int x = ox + i * 22, y = ch - 62;
            s_fill(s, x, y, 20, 20, i); s_rect(s, x, y, 20, 20, i == col ? COL_WHITE : COL_BLACK);
            if (i == col) s_rect(s, x + 1, y + 1, 18, 18, COL_BLACK);
        }
        ui_button(s, ox, ch - 30, 56, 22, "Save", 0, 0);
        ui_button(s, ox + 62, ch - 30, 56, 22, "Open", 0, 0);
        ui_button(s, ox + 124, ch - 30, 56, 22, "Clear", 0, 0);
        s_text(s, ox + 190, ch - 26, status, COL_DARK_GREY, COL_NONE);
        s_bevel(s, ox - 2, oy - 2, CW + 4, CH + 4, 1);
        s_blit(s, ox, oy, &canvas, 0, 0, CW, CH);
        if (drag && tool >= T_LINE && tool <= T_ELL) {
            surface_t over = *s;
            surf_set_clip(&over, ox, oy, CW, CH);
            shape(&over, tool, sx + ox, sy + oy, ex + ox, ey + oy, col);
        }
        wm_invalidate(win);

        event_t ev;
        wm_next_event(win, &ev, -1);
        if (ev.type == EV_CLOSE) { kfree(buf); wm_destroy(win); return; }
        int cx = ev.a - ox, cy = ev.b - oy;
        int inside = cx >= 0 && cy >= 0 && cx < CW && cy < CH;
        if (ev.type == EV_MOUSE_DOWN && ev.c == 1) {
            if (inside) {
                sx = ex = cx; sy = ey = cy; drag = 1;
                if (tool == T_FILL) { paint_flood(buf, CW, CH, cx, cy, col); drag = 0; }
                else if (tool == T_PEN || tool == T_ERASE) s_fill(&canvas, cx - size / 2, cy - size / 2, size, size, tool == T_ERASE ? COL_WHITE : col);
            } else {
                for (int i = 0; i < T_COUNT; i++) if (ui_inside(ev.a, ev.b, 4, 6 + i * 26, 52, 24)) tool = i;
                if (ui_inside(ev.a, ev.b, 4, 196, 24, 22) && size > 1) size--;
                if (ui_inside(ev.a, ev.b, 32, 196, 24, 22) && size < 12) size++;
                for (int i = 0; i < 16; i++) if (ui_inside(ev.a, ev.b, ox + i * 22, ch - 62, 20, 20)) col = i;
                if (ui_inside(ev.a, ev.b, ox, ch - 30, 56, 22)) {
                    u32 n; u8 *b = bmp_encode(buf, CW, CH, &n);
                    s_fill(s, ox + 190, ch - 30, 170, 22, COL_LIGHT_GREY);
                    s_text(s, ox + 190, ch - 26, "Saving...", COL_DARK_GREY, COL_NONE);
                    wm_invalidate(win);
                    if (b) {
                        kstrlcpy(status, fat16_write_file("PAINT.BMP", b, n) ? "Saved PAINT.BMP" : "Save failed", sizeof(status));   /* 1 = success */
                        kfree(b);
                    }
                }
                if (ui_inside(ev.a, ev.b, ox + 62, ch - 30, 56, 22)) {
                    image_t im;
                    s_fill(s, ox + 190, ch - 30, 170, 22, COL_LIGHT_GREY);
                    s_text(s, ox + 190, ch - 26, "Opening...", COL_DARK_GREY, COL_NONE);
                    wm_invalidate(win);
                    if (load_bmp_file("PAINT.BMP", &im) == 0) {
                        memset(buf, COL_WHITE, CW * CH);
                        for (int y = 0; y < im.h && y < CH; y++) for (int x = 0; x < im.w && x < CW; x++) buf[y * CW + x] = im.px[y * im.w + x];
                        kfree(im.px); kstrlcpy(status, "Opened PAINT.BMP", sizeof(status));
                    } else kstrlcpy(status, "No PAINT.BMP", sizeof(status));
                }
                if (ui_inside(ev.a, ev.b, ox + 124, ch - 30, 56, 22)) memset(buf, COL_WHITE, CW * CH);
            }
        } else if (ev.type == EV_MOUSE_MOVE && drag) {
            int nx = cx < 0 ? 0 : cx >= CW ? CW - 1 : cx, ny = cy < 0 ? 0 : cy >= CH ? CH - 1 : cy;
            if (tool == T_PEN || tool == T_ERASE) {
                int c = tool == T_ERASE ? COL_WHITE : col;
                if (size == 1) s_line(&canvas, ex, ey, nx, ny, c);
                else { /* thick stroke: stamp along the segment */
                    int dx = nx - ex, dy = ny - ey, st = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
                    for (int i = 0; i <= st; i++) s_fill(&canvas, ex + (st ? dx * i / st : 0) - size / 2, ey + (st ? dy * i / st : 0) - size / 2, size, size, c);
                }
                sx = nx; sy = ny;
            }
            ex = nx; ey = ny;
        } else if (ev.type == EV_MOUSE_UP && drag) {
            if (tool >= T_LINE && tool <= T_ELL) shape(&canvas, tool, sx, sy, ex, ey, col);
            drag = 0;
        }
    }
}

/* ========================================================== Image Viewer == */

void app_view_main(void *arg)
{
    window_t *win = wm_create("Image Viewer", 560, 380, WF_RESIZABLE, &view_def);
    if (!win) return;
    wm_set_min_size(win, 300, 200);
    fat16_entry_t ents[64];
    int idx[64], ni = 0, sel = -1;
    {   /* show something right away: the disk may be busy (e.g. Paint is saving) */
        surface_t *s0 = wm_surface(win);
        int c0, h0;
        wm_client_size(win, &c0, &h0);
        s_fill(s0, 0, 0, c0, h0, COL_LIGHT_GREY);
        s_text(s0, 12, 12, "Reading disk...", COL_DARK_GREY, COL_NONE);
        wm_invalidate(win);
        int n = fat16_read_dir(ents, 64);
        for (int i = 0; i < n; i++) {
            int l = (int)kstrlen(ents[i].name);
            if (l > 4 && kstricmp(ents[i].name + l - 4, ".BMP") == 0) idx[ni++] = i;
        }
    }
    image_t img = { 0, 0, 0 };
    char msg[40] = "Select a BMP file";
    if (arg) { for (int i = 0; i < ni; i++) if (kstricmp(ents[idx[i]].name, (const char *)arg) == 0) sel = i; }
    int need_load = sel >= 0;
    for (;;) {
        if (need_load) {
            need_load = 0;
            if (img.px) { kfree(img.px); img.px = 0; }
            int e = load_bmp_file(ents[idx[sel]].name, &img);
            if (e) ksnprintf(msg, sizeof(msg), "Cannot decode (error %d)", e);
            else ksnprintf(msg, sizeof(msg), "%s  %dx%d", ents[idx[sel]].name, img.w, img.h);
        }
        surface_t *s = wm_surface(win);
        int cw, ch;
        wm_client_size(win, &cw, &ch);
        if (s->w >= cw && s->h >= ch) {
            s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);
            ui_panel(s, 4, 4, 130, ch - 28, 1);
            for (int i = 0; i < ni && i < (ch - 36) / 18; i++) {
                if (i == sel) s_fill(s, 6, 6 + i * 18, 126, 18, COL_BLUE);
                s_text(s, 10, 7 + i * 18, ents[idx[i]].name, i == sel ? COL_WHITE : COL_BLACK, COL_NONE);
            }
            int vx = 140, vw = cw - vx - 4, vh = ch - 28;
            s_fill(s, vx, 4, vw, vh, COL_BLACK);
            if (img.px) {
                int sw = img.w, sh = img.h;   /* integer shrink to fit (nearest neighbour) */
                int dw = sw, dh = sh;
                if (sw < vw && sh < vh) {      /* small image: whole-number zoom to fill the pane */
                    int z = 1;
                    while (sw * (z + 1) <= vw && sh * (z + 1) <= vh) z++;
                    dw = sw * z; dh = sh * z;
                }
                while (dw > vw || dh > vh) { dw = dw * 3 / 4; dh = dh * 3 / 4; }
                if (dw < 1) dw = 1;
                if (dh < 1) dh = 1;
                int ox = vx + (vw - dw) / 2, oy = 4 + (vh - dh) / 2;
                for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++)
                    s_pixel(s, ox + x, oy + y, img.px[(y * sh / dh) * sw + (x * sw / dw)]);
            }
            s_text(s, 8, ch - 20, msg, COL_BLACK, COL_NONE);
            wm_invalidate(win);
        }
        event_t ev;
        wm_next_event(win, &ev, -1);
        if (ev.type == EV_CLOSE) { if (img.px) kfree(img.px); wm_destroy(win); return; }
        if (ev.type == EV_RESIZE) wm_resize_client(win, ev.a, ev.b);
        if (ev.type == EV_MOUSE_DOWN && ev.c == 1 && ev.a < 134 && ev.b >= 6) {
            int i = (ev.b - 6) / 18;
            if (i < ni) { sel = i; need_load = 1; }
        }
        if (ev.type == EV_KEY && ni) {
            if (ev.a == KEY_DOWN && sel < ni - 1) { sel++; need_load = 1; }
            if (ev.a == KEY_UP && sel > 0) { sel--; need_load = 1; }
        }
    }
}
