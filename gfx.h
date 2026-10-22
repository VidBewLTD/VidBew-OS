#ifndef GFX_H
#define GFX_H

/* gfx.h -- VidBew/OS graphics core.
 *
 * Everything the UI draws lives in 8-bit "indexed" surfaces: each pixel is a
 * palette index. The window manager composes all windows into one screen-
 * sized backbuffer (gfx_screen) and gfx_present() converts the changed
 * rectangles to whatever the real display wants (8-bit palette via the VGA
 * DAC, or 16/24/32-bit true colour through a lookup table). That's why one
 * UI works unchanged at every display type.
 *
 * Palette: 0-15 are the classic EGA/VGA colours (the COL_* names), 16-231 a
 * 6x6x6 colour cube, 232-255 a grey ramp. */

#include "types.h"
#include "bootinfo.h"

#define COL_BLACK         0
#define COL_BLUE          1
#define COL_GREEN         2
#define COL_CYAN          3
#define COL_RED           4
#define COL_MAGENTA       5
#define COL_BROWN         6
#define COL_LIGHT_GREY    7
#define COL_DARK_GREY     8
#define COL_LIGHT_BLUE    9
#define COL_LIGHT_GREEN   10
#define COL_LIGHT_CYAN    11
#define COL_LIGHT_RED     12
#define COL_LIGHT_MAGENTA 13
#define COL_YELLOW        14
#define COL_WHITE         15
#define COL_NONE          (-1)   /* "transparent" where a colour is optional */

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

typedef struct {
    int w, h;
    u8 *px;                      /* w*h palette indices, pitch == w */
    int cx0, cy0, cx1, cy1;      /* clip rectangle, x1/y1 exclusive */
} surface_t;

extern surface_t gfx_screen;     /* the compositor's backbuffer */
#define SCREEN_WIDTH  (gfx_screen.w)
#define SCREEN_HEIGHT (gfx_screen.h)

/* ---- display / modes ---- */
int  gfx_init(void);                                /* after heap_init */
int  gfx_mode_count(void);
const vbe_mode_t *gfx_mode_at(int i);
int  gfx_cur_bpp(void);
int  gfx_cur_mode_number(void);
int  gfx_live_capable(void);                        /* Bochs/QEMU/VirtualBox VBE ports present */
int  gfx_set_mode_live(int w, int h, int bpp);      /* 1 = switched */
void gfx_present(int x, int y, int w, int h);       /* backbuffer rect -> framebuffer (+ cursor) */
void gfx_cursor_show(int visible);
void gfx_cursor_move(int x, int y);
void gfx_panic(const char *msg);                    /* draw an error banner straight to the display */
const char *gfx_depth_name(int bpp);

/* ---- surfaces ---- */
void surf_init(surface_t *s, int w, int h, u8 *px);
void surf_set_clip(surface_t *s, int x, int y, int w, int h);
void surf_reset_clip(surface_t *s);

void s_pixel(surface_t *s, int x, int y, int c);
void s_fill(surface_t *s, int x, int y, int w, int h, int c);
void s_rect(surface_t *s, int x, int y, int w, int h, int c);      /* 1px outline */
void s_hline(surface_t *s, int x, int y, int w, int c);
void s_vline(surface_t *s, int x, int y, int h, int c);
void s_line(surface_t *s, int x0, int y0, int x1, int y1, int c);
void s_circle(surface_t *s, int cx, int cy, int r, int c);
void s_fcircle(surface_t *s, int cx, int cy, int r, int c);
void s_char(surface_t *s, int x, int y, int ch, int fg, int bg);
void s_text(surface_t *s, int x, int y, const char *str, int fg, int bg);
void s_text_n(surface_t *s, int x, int y, const char *str, int maxchars, int fg, int bg);
int  s_text_width(const char *str);
void s_blit(surface_t *dst, int dx, int dy, const surface_t *src, int sx, int sy, int w, int h);
void s_bevel(surface_t *s, int x, int y, int w, int h, int sunken);   /* 2px 3D border */
void s_bevel1(surface_t *s, int x, int y, int w, int h, int sunken);  /* 1px 3D border */
u8   gfx_rgb(int r, int g, int b);                                    /* nearest palette index */
void gfx_palette_rgb(int idx, int *r, int *g, int *b);                 /* palette entry -> 0..255 RGB */

#endif
