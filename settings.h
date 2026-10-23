#ifndef SETTINGS_H
#define SETTINGS_H

#include "types.h"

/* Persistent settings live in one raw sector (LBA 2047), written by the
 * Control Panel. The bootloader reads the first few fields before the kernel
 * even starts, so the chosen display mode is applied from the very first
 * frame. Layout is fixed -- see stage2.asm. */
#define SETTINGS_LBA 2047

typedef struct __attribute__((packed)) {
    u32 magic;            /* 'VBOS' */
    u16 version;
    u16 disp_w, disp_h;
    u8  disp_bpp;
    u8  scheme;
    u8  dbl_ticks;        /* double-click interval in 10ms ticks */
    u8  volume;           /* 0-100 */
    u16 mouse_pct;        /* pointer speed, percent (50-300) */
    u8  net_dhcp;
    u8  ip[4], mask[4], gw[4], dns[4];
    u8  boot_chime;       /* 1 = play chime at boot */
    u8  show_bootscreen;  /* 1 = show VBAI bootscreen splash */
    u8  taskbar_pos;      /* 0 = bottom, 1 = top */
    u8  taskbar_autohide; /* 0 = off, 1 = on */
    u8  show_menubar;     /* 1 = visible, 0 = hidden */
    u8  pad[26];
} settings_t;

extern settings_t g_settings;

void settings_load(void);
int  settings_save(void);
void settings_apply(void);

#endif
