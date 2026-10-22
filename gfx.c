/* gfx.c -- graphics core: surfaces, palette, display output, cursor.
 *
 * The video mode itself is chosen by stage2.asm via VBE (the video BIOS isn't
 * reachable from protected mode). When the emulator exposes the "Bochs VBE"
 * I/O ports (QEMU, Bochs, VirtualBox) we can also switch modes live. */

#include "gfx.h"
#include "font8x16.h"
#include "io.h"
#include "heap.h"
#include "kstd.h"
#include "serial.h"
#include "vbai_logo.h"

surface_t gfx_screen;

static u8 *fb = 0;
static int fb_pitch = 0, fb_bpp = 8, fb_bytes = 1;
static int fb_w = 0, fb_h = 0;
static int fb_mode = 0;
static int rsize, rpos, gsize, gpos, bsize, bpos;
static u8 pal[256][3];
static u32 lut[256];

#define MAX_MODES 64
static vbe_mode_t modes[MAX_MODES];
static int mode_count = 0;
static int dispi_present = 0;

/* ---------------- palette ---------------- */

static void build_palette(void)
{
    static const u8 ega[16][3] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    static const u8 lv[6] = {0, 51, 102, 153, 204, 255};
    for (int i = 0; i < 16; i++) for (int k = 0; k < 3; k++) pal[i][k] = ega[i][k];
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                int i = 16 + 36 * r + 6 * g + b;
                pal[i][0] = lv[r]; pal[i][1] = lv[g]; pal[i][2] = lv[b];
            }
    for (int i = 0; i < 24; i++) {
        u8 v = (u8)(8 + 10 * i);
        pal[232 + i][0] = pal[232 + i][1] = pal[232 + i][2] = v;
    }
}

u8 gfx_rgb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    if (r == g && g == b) {
        int i = (r - 3) / 10;
        if (i < 0) i = 0;
        if (i > 23) i = 23;
        return (u8)(232 + i);
    }
    return (u8)(16 + 36 * ((r * 5 + 127) / 255) + 6 * ((g * 5 + 127) / 255) + ((b * 5 + 127) / 255));
}

static void dac_program(void)
{
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        outb(0x3C9, (u8)(pal[i][0] >> 2));
        outb(0x3C9, (u8)(pal[i][1] >> 2));
        outb(0x3C9, (u8)(pal[i][2] >> 2));
    }
}

static void build_lut(void)
{
    if (fb_bpp == 8) {
        for (int i = 0; i < 256; i++) lut[i] = (u32)i;
        dac_program();
        return;
    }
    for (int i = 0; i < 256; i++) {
        u32 r = pal[i][0], g = pal[i][1], b = pal[i][2];
        lut[i] = ((r >> (8 - rsize)) << rpos) | ((g >> (8 - gsize)) << gpos) | ((b >> (8 - bsize)) << bpos);
    }
}

static void set_default_masks(int bpp)
{
    switch (bpp) {
    case 15: rsize = 5; rpos = 10; gsize = 5; gpos = 5; bsize = 5; bpos = 0; break;
    case 16: rsize = 5; rpos = 11; gsize = 6; gpos = 5; bsize = 5; bpos = 0; break;
    default: rsize = 8; rpos = 16; gsize = 8; gpos = 8; bsize = 8; bpos = 0; break; /* 24/32 */
    }
}

/* ---------------- display output ---------------- */

static void fb_set_format(int bpp)
{
    fb_bpp = bpp;
    fb_bytes = (bpp + 7) / 8;
}

static void fb_put(int x, int y, int color)
{
    if (!fb || x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    u8 *d = fb + y * fb_pitch + x * fb_bytes;
    u32 v = lut[color & 255];
    switch (fb_bytes) {
    case 1: d[0] = (u8)v; break;
    case 2: *(u16 *)d = (u16)v; break;
    case 3: d[0] = (u8)v; d[1] = (u8)(v >> 8); d[2] = (u8)(v >> 16); break;
    default: *(u32 *)d = v; break;
    }
}

static void present_raw(int x, int y, int w, int h)
{
    if (!fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gfx_screen.w) w = gfx_screen.w - x;
    if (y + h > gfx_screen.h) h = gfx_screen.h - y;
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;

    for (int row = y; row < y + h; row++) {
        const u8 *src = gfx_screen.px + row * gfx_screen.w + x;
        u8 *dst = fb + row * fb_pitch + x * fb_bytes;
        switch (fb_bytes) {
        case 1:
            memcpy(dst, src, (unsigned)w);
            break;
        case 2: {
            u16 *d = (u16 *)dst;
            for (int i = 0; i < w; i++) d[i] = (u16)lut[src[i]];
            break;
        }
        case 3:
            for (int i = 0; i < w; i++) {
                u32 v = lut[src[i]];
                dst[3 * i] = (u8)v; dst[3 * i + 1] = (u8)(v >> 8); dst[3 * i + 2] = (u8)(v >> 16);
            }
            break;
        default: {
            u32 *d = (u32 *)dst;
            for (int i = 0; i < w; i++) d[i] = lut[src[i]];
            break;
        }
        }
    }
}

/* ---------------- cursor (drawn on the display only, never in the backbuffer) ---------------- */

#define CURSOR_W 11
#define CURSOR_H 15
static const u8 cursor_mask[CURSOR_H][CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0,0,0},{1,1,1,0,0,0,0,0,0,0,0},
    {1,1,1,1,0,0,0,0,0,0,0},{1,1,1,1,1,0,0,0,0,0,0},{1,1,1,1,1,1,0,0,0,0,0},
    {1,1,1,1,1,1,1,0,0,0,0},{1,1,1,1,1,1,1,1,0,0,0},{1,1,1,1,1,1,1,1,1,0,0},
    {1,1,1,1,1,1,1,1,1,1,0},{1,1,1,1,1,1,1,1,1,1,1},{1,1,1,1,0,0,0,0,0,0,0},
    {1,1,1,0,0,0,0,0,0,0,0},{1,1,0,0,1,1,0,0,0,0,0},{1,0,0,0,1,1,0,0,0,0,0},
};
static int cur_x = 0, cur_y = 0, cur_visible = 0;

static int cursor_neighbour(int y, int x)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int ny = y + dy, nx = x + dx;
            if (ny >= 0 && ny < CURSOR_H && nx >= 0 && nx < CURSOR_W && cursor_mask[ny][nx]) return 1;
        }
    return 0;
}

static void cursor_paint(void)
{
    for (int row = 0; row < CURSOR_H; row++)
        for (int col = 0; col < CURSOR_W; col++) {
            if (cursor_mask[row][col]) fb_put(cur_x + col, cur_y + row, COL_BLACK);
            else if (cursor_neighbour(row, col)) fb_put(cur_x + col, cur_y + row, COL_WHITE);
        }
}

void gfx_present(int x, int y, int w, int h)
{
    present_raw(x, y, w, h);
    if (cur_visible && x < cur_x + CURSOR_W + 1 && x + w > cur_x - 1 &&
        y < cur_y + CURSOR_H + 1 && y + h > cur_y - 1)
        cursor_paint();
}

void gfx_cursor_show(int visible)
{
    if (visible == cur_visible) return;
    if (!visible) {
        cur_visible = 0;
        present_raw(cur_x - 1, cur_y - 1, CURSOR_W + 2, CURSOR_H + 2);
    } else {
        cur_visible = 1;
        cursor_paint();
    }
}

void gfx_cursor_move(int x, int y)
{
    if (x == cur_x && y == cur_y) return;
    if (cur_visible) present_raw(cur_x - 1, cur_y - 1, CURSOR_W + 2, CURSOR_H + 2);
    cur_x = x;
    cur_y = y;
    if (cur_visible) cursor_paint();
}

/* ---------------- init / modes ---------------- */

static u16 dispi_read(u16 idx) { outw(0x1CE, idx); return inw(0x1CF); }
static void dispi_write(u16 idx, u16 val) { outw(0x1CE, idx); outw(0x1CF, val); }

const char *gfx_depth_name(int bpp)
{
    switch (bpp) {
    case 1:  return "Monochrome (1-bit)";
    case 4:  return "16 colors (4-bit)";
    case 8:  return "256 colors (8-bit)";
    case 15: return "32,768 colors (15-bit)";
    case 16: return "65,536 colors (16-bit)";
    case 24: return "True Color (24-bit)";
    case 32: return "True Color (32-bit)";
    default: return "?";
    }
}

int gfx_init(void)
{
    const volatile boot_info_t *bi = BOOT_INFO;
    fb = (u8 *)bi->fb;
    fb_w = bi->width;
    fb_h = bi->height;
    fb_pitch = bi->pitch;
    fb_mode = bi->mode;
    fb_set_format(bi->bpp);
    rsize = bi->rsize; rpos = bi->rpos; gsize = bi->gsize; gpos = bi->gpos; bsize = bi->bsize; bpos = bi->bpos;
    if (fb_bpp != 8 && (rsize == 0 || gsize == 0 || bsize == 0)) set_default_masks(fb_bpp);

    mode_count = bi->mode_count;
    if (mode_count > MAX_MODES) mode_count = MAX_MODES;
    for (int i = 0; i < mode_count; i++) {
        modes[i].mode = BOOT_MODE_TABLE[i].mode;
        modes[i].w = BOOT_MODE_TABLE[i].w;
        modes[i].h = BOOT_MODE_TABLE[i].h;
        modes[i].bpp = BOOT_MODE_TABLE[i].bpp;
        modes[i].model = BOOT_MODE_TABLE[i].model;
    }

    u16 id = dispi_read(0);
    dispi_present = (id >= 0xB0C0 && id <= 0xB0CF);

    build_palette();
    build_lut();

    u8 *back = (u8 *)kmalloc((unsigned)(fb_w * fb_h));
    if (!back) return 0;
    surf_init(&gfx_screen, fb_w, fb_h, back);
    memset(back, COL_CYAN, (unsigned)(fb_w * fb_h));
    return 1;
}

int gfx_mode_count(void) { return mode_count; }
const vbe_mode_t *gfx_mode_at(int i) { return (i >= 0 && i < mode_count) ? &modes[i] : 0; }
int gfx_cur_bpp(void) { return fb_bpp; }
int gfx_cur_mode_number(void) { return fb_mode; }
int gfx_live_capable(void) { return dispi_present; }

int gfx_set_mode_live(int w, int h, int bpp)
{
    if (!dispi_present) return 0;
    if (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return 0;

    u8 *back = (u8 *)kmalloc((unsigned)(w * h));
    if (!back) return 0;

    int old_w = fb_w, old_h = fb_h, old_bpp = fb_bpp;

    dispi_write(4, 0);
    dispi_write(3, (u16)bpp);
    dispi_write(1, (u16)w);
    dispi_write(2, (u16)h);
    dispi_write(6, (u16)w);
    dispi_write(8, 0);
    dispi_write(9, 0);
    dispi_write(4, 0x41);

    if (dispi_read(1) != w || dispi_read(2) != h || dispi_read(3) != bpp) {
        /* refused: put the old mode back */
        dispi_write(4, 0);
        dispi_write(3, (u16)old_bpp);
        dispi_write(1, (u16)old_w);
        dispi_write(2, (u16)old_h);
        dispi_write(6, (u16)old_w);
        dispi_write(4, 0x41);
        kfree(back);
        return 0;
    }

    fb_w = w;
    fb_h = h;
    fb_set_format(bpp);
    fb_pitch = w * fb_bytes;
    fb_mode = 0;
    if (bpp != 8) set_default_masks(bpp);
    build_lut();

    u8 *old = gfx_screen.px;
    surf_init(&gfx_screen, w, h, back);
    memset(back, COL_CYAN, (unsigned)(w * h));
    kfree(old);
    return 1;
}

/* ---------------- boot splash ---------------- */

void gfx_bootscreen(int percent, const char *status)
{
    if (!fb) return;
    surface_t *s = &gfx_screen;
    s_fill(s, 0, 0, s->w, s->h, COL_BLACK);

    /* Draw centered VBAI Logo */
    int lx = (s->w - VBAI_LOGO_W) / 2;
    int ly = (s->h - VBAI_LOGO_H) / 2 - 20;
    if (lx >= 0 && ly >= 0) {
        for (int row = 0; row < VBAI_LOGO_H; row++) {
            for (int col = 0; col < VBAI_LOGO_W; col++) {
                u8 c = vbai_logo_px[row * VBAI_LOGO_W + col];
                s_pixel(s, lx + col, ly + row, c);
            }
        }
    }

    /* Status and Title */
    const char *title = "VidBew/OS Starting...";
    s_text(s, (s->w - s_text_width(title)) / 2, ly + VBAI_LOGO_H + 20, title, COL_WHITE, COL_NONE);

    if (status && *status) {
        s_text(s, (s->w - s_text_width(status)) / 2, ly + VBAI_LOGO_H + 42, status, COL_LIGHT_CYAN, COL_NONE);
    }

    /* Progress bar */
    int pw = 240, ph = 12;
    int px = (s->w - pw) / 2, py = ly + VBAI_LOGO_H + 65;
    s_rect(s, px, py, pw, ph, COL_WHITE);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int fill_w = (pw - 4) * percent / 100;
    if (fill_w > 0) s_fill(s, px + 2, py + 2, fill_w, ph - 4, COL_LIGHT_BLUE);

    gfx_present(0, 0, s->w, s->h);
}

/* ---------------- BSOD / Panic Screen ---------------- */

void gfx_panic(const char *msg)
{
    if (!fb) return;
    surface_t *s = &gfx_screen;
    s_fill(s, 0, 0, s->w, s->h, COL_BLUE);

    /* Center Box */
    int bw = s->w > 500 ? 500 : s->w - 20;
    int bh = s->h > 320 ? 320 : s->h - 20;
    int bx = (s->w - bw) / 2, by = (s->h - bh) / 2;

    s_fill(s, bx, by, bw, bh, COL_BLUE);
    s_rect(s, bx, by, bw, bh, COL_WHITE);
    s_rect(s, bx + 2, by + 2, bw - 4, bh - 4, COL_WHITE);

    /* Title */
    const char *title = " VidBew/OS -- Fatal System Error ";
    s_text(s, bx + (bw - s_text_width(title)) / 2, by + 12, title, COL_BLUE, COL_WHITE);

    s_text(s, bx + 16, by + 42, "A fatal exception or kernel panic has occurred.", COL_WHITE, COL_NONE);
    s_text(s, bx + 16, by + 60, "The current task or kernel halted to prevent damage.", COL_WHITE, COL_NONE);

    s_hline(s, bx + 10, by + 85, bw - 20, COL_WHITE);

    s_text(s, bx + 16, by + 100, "Error Information:", COL_YELLOW, COL_NONE);
    if (msg) {
        s_text(s, bx + 16, by + 120, msg, COL_WHITE, COL_NONE);
    }

    s_hline(s, bx + 10, by + 200, bw - 20, COL_WHITE);

    s_text(s, bx + 16, by + 220, "* Press Ctrl+Alt+Del or restart your machine.", COL_LIGHT_GREY, COL_NONE);
    s_text(s, bx + 16, by + 240, "* System halted safely.", COL_LIGHT_GREY, COL_NONE);

    gfx_present(0, 0, s->w, s->h);
}

/* ---------------- surfaces ---------------- */

void surf_init(surface_t *s, int w, int h, u8 *px)
{
    s->w = w; s->h = h; s->px = px;
    s->cx0 = 0; s->cy0 = 0; s->cx1 = w; s->cy1 = h;
}

void surf_set_clip(surface_t *s, int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 < x) x1 = x;
    if (y1 < y) y1 = y;
    s->cx0 = x; s->cy0 = y; s->cx1 = x1; s->cy1 = y1;
}

void surf_reset_clip(surface_t *s)
{
    s->cx0 = 0; s->cy0 = 0; s->cx1 = s->w; s->cy1 = s->h;
}

void s_pixel(surface_t *s, int x, int y, int c)
{
    if (c < 0 || x < s->cx0 || y < s->cy0 || x >= s->cx1 || y >= s->cy1) return;
    s->px[y * s->w + x] = (u8)c;
}

void s_fill(surface_t *s, int x, int y, int w, int h, int c)
{
    if (c < 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < s->cx0) x = s->cx0;
    if (y < s->cy0) y = s->cy0;
    if (x1 > s->cx1) x1 = s->cx1;
    if (y1 > s->cy1) y1 = s->cy1;
    if (x >= x1 || y >= y1) return;
    unsigned n = (unsigned)(x1 - x);
    for (int row = y; row < y1; row++) memset(s->px + row * s->w + x, c, n);
}

void s_hline(surface_t *s, int x, int y, int w, int c) { s_fill(s, x, y, w, 1, c); }
void s_vline(surface_t *s, int x, int y, int h, int c) { s_fill(s, x, y, 1, h, c); }

void s_rect(surface_t *s, int x, int y, int w, int h, int c)
{
    if (w <= 0 || h <= 0) return;
    s_fill(s, x, y, w, 1, c);
    s_fill(s, x, y + h - 1, w, 1, c);
    s_fill(s, x, y, 1, h, c);
    s_fill(s, x + w - 1, y, 1, h, c);
}

void s_line(surface_t *s, int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;   /* negative */
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        s_pixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void s_circle(surface_t *s, int cx, int cy, int r, int c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        s_pixel(s, cx + x, cy + y, c); s_pixel(s, cx - x, cy + y, c);
        s_pixel(s, cx + x, cy - y, c); s_pixel(s, cx - x, cy - y, c);
        s_pixel(s, cx + y, cy + x, c); s_pixel(s, cx - y, cy + x, c);
        s_pixel(s, cx + y, cy - x, c); s_pixel(s, cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void s_fcircle(surface_t *s, int cx, int cy, int r, int c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        s_hline(s, cx - x, cy + y, 2 * x + 1, c); s_hline(s, cx - x, cy - y, 2 * x + 1, c);
        s_hline(s, cx - y, cy + x, 2 * y + 1, c); s_hline(s, cx - y, cy - x, 2 * y + 1, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void s_char(surface_t *s, int x, int y, int ch, int fg, int bg)
{
    if (x >= s->cx1 || x + FONT_WIDTH <= s->cx0 || y >= s->cy1 || y + FONT_HEIGHT <= s->cy0) return;
    if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR) ch = ' ';
    const u8 *g = font8x16[ch - FONT_FIRST_CHAR];
    int inside = (x >= s->cx0 && x + FONT_WIDTH <= s->cx1);
    for (int r = 0; r < FONT_HEIGHT; r++) {
        int py = y + r;
        if (py < s->cy0 || py >= s->cy1) continue;
        u8 bits = g[r];
        u8 *row = s->px + py * s->w;
        if (inside) {
            for (int c = 0; c < FONT_WIDTH; c++) {
                if (bits & (0x80 >> c)) { if (fg >= 0) row[x + c] = (u8)fg; }
                else if (bg >= 0) row[x + c] = (u8)bg;
            }
        } else {
            for (int c = 0; c < FONT_WIDTH; c++) {
                int px = x + c;
                if (px < s->cx0 || px >= s->cx1) continue;
                if (bits & (0x80 >> c)) { if (fg >= 0) row[px] = (u8)fg; }
                else if (bg >= 0) row[px] = (u8)bg;
            }
        }
    }
}

void s_text_n(surface_t *s, int x, int y, const char *str, int maxchars, int fg, int bg)
{
    for (int i = 0; str[i] && (maxchars < 0 || i < maxchars); i++) {
        if (x + i * FONT_WIDTH >= s->cx1) break;
        s_char(s, x + i * FONT_WIDTH, y, (unsigned char)str[i], fg, bg);
    }
}

void s_text(surface_t *s, int x, int y, const char *str, int fg, int bg)
{
    s_text_n(s, x, y, str, -1, fg, bg);
}

int s_text_width(const char *str)
{
    return (int)kstrlen(str) * FONT_WIDTH;
}

void s_blit(surface_t *d, int dx, int dy, const surface_t *src, int sx, int sy, int w, int h)
{
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (dx < d->cx0) { int k = d->cx0 - dx; sx += k; w -= k; dx = d->cx0; }
    if (dy < d->cy0) { int k = d->cy0 - dy; sy += k; h -= k; dy = d->cy0; }
    if (dx + w > d->cx1) w = d->cx1 - dx;
    if (dy + h > d->cy1) h = d->cy1 - dy;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++)
        memcpy(d->px + (dy + row) * d->w + dx, src->px + (sy + row) * src->w + sx, (unsigned)w);
}

void s_bevel(surface_t *s, int x, int y, int w, int h, int sunken)
{
    int tl1 = sunken ? COL_DARK_GREY : COL_WHITE;
    int br1 = sunken ? COL_WHITE : COL_BLACK;
    int tl2 = sunken ? COL_BLACK : COL_LIGHT_GREY;
    int br2 = sunken ? COL_LIGHT_GREY : COL_DARK_GREY;
    s_hline(s, x, y, w, tl1);            s_vline(s, x, y, h, tl1);
    s_hline(s, x, y + h - 1, w, br1);    s_vline(s, x + w - 1, y, h, br1);
    s_hline(s, x + 1, y + 1, w - 2, tl2);         s_vline(s, x + 1, y + 1, h - 2, tl2);
    s_hline(s, x + 1, y + h - 2, w - 2, br2);     s_vline(s, x + w - 2, y + 1, h - 2, br2);
}

void s_bevel1(surface_t *s, int x, int y, int w, int h, int sunken)
{
    int tl = sunken ? COL_DARK_GREY : COL_WHITE;
    int br = sunken ? COL_WHITE : COL_DARK_GREY;
    s_hline(s, x, y, w, tl);          s_vline(s, x, y, h, tl);
    s_hline(s, x, y + h - 1, w, br);  s_vline(s, x + w - 1, y, h, br);
}

void gfx_palette_rgb(int idx, int *r, int *g, int *b)
{
    idx &= 255;
    *r = pal[idx][0]; *g = pal[idx][1]; *b = pal[idx][2];
}
