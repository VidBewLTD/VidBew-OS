#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "types.h"

/* Hand-off block left at 0x7E00 by stage2.asm. */
typedef struct __attribute__((packed)) {
    u32 fb;                 /* linear framebuffer physical address */
    u16 width, height, pitch;
    u8  bpp, model;
    u16 mode;               /* VBE mode number */
    u8  rsize, rpos, gsize, gpos, bsize, bpos;
    u16 mode_count;         /* entries in the table at BOOT_MODE_TABLE */
    u32 ext_kb;             /* KB of RAM above 1MB (BIOS) */
    u8  boot_drive;
    u8  flags;              /* 1 = settings sector found, 2 = saved mode used, 4 = fell back */
} boot_info_t;

typedef struct __attribute__((packed)) {
    u16 mode, w, h;
    u8  bpp, model;
} vbe_mode_t;

#define BOOT_INFO ((const volatile boot_info_t *)0x7E00)
#define BOOT_MODE_TABLE ((const volatile vbe_mode_t *)0x7000)

#endif
