/* kernel.c -- VidBew/OS: Heroine kernel start-up and the UI task loop.
 *
 * "Nova" is the graphical shell (wm.c and the apps); "Heroine" is the kernel
 * underneath. This file brings the machine up in dependency order, starts the
 * first windows, and then becomes the UI task: it wakes on every interrupt,
 * lets the window manager route input and repaint, and sleeps in `hlt`
 * otherwise. Every app and the network daemon are separate tasks. */

#include "gfx.h"
#include "idt.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "heap.h"
#include "timer.h"
#include "rtc.h"
#include "ata.h"
#include "fat16.h"
#include "task.h"
#include "input_mode.h"
#include "sb16.h"
#include "audio.h"
#include "wm.h"
#include "apps.h"
#include "net.h"
#include "settings.h"
#include "kstd.h"

#define FAT_PARTITION_LBA 2048

static void run_filesystem_demo(void)
{
    /* Proves read+write end to end at boot, as before. With the new
     * replace-on-write behaviour this no longer piles up duplicate files. */
    static const char msg[] = "Written by the VidBew/OS Heroine kernel at boot.";
    unsigned char buf[128];
    unsigned int n = 0;
    if (!fat16_write_file("WRITTEN.TXT", (const unsigned char *)msg, sizeof(msg) - 1)) {
        serial_print("fat16: write FAILED\n");
        return;
    }
    if (fat16_read_file("WRITTEN.TXT", buf, sizeof(buf), &n) && n == sizeof(msg) - 1 && memcmp(buf, msg, n) == 0)
        serial_print("fat16: write/read-back verified\n");
    else
        serial_print("fat16: read-back MISMATCH\n");
}

void kmain(void)
{
    serial_init();
    serial_print("Heroine kernel booting -- VidBew/OS.\n");

    const volatile boot_info_t *bi = BOOT_INFO;
    unsigned heap_size = bi->ext_kb * 1024u;
    if (heap_size > 32u * 1024 * 1024) heap_size = 32u * 1024 * 1024;
    if (heap_size < 3u * 1024 * 1024) heap_size = 3u * 1024 * 1024;
    heap_init(0x100000, heap_size - 0x10000);
    heap_selftest();

    if (!gfx_init()) {
        serial_print("FATAL: could not set up the display\n");
        for (;;) __asm__ volatile("hlt");
    }
    char m[120];
    ksnprintf(m, sizeof(m), "display: %dx%d, %d bpp (%s), %d modes available, live switching %s\n",
              gfx_screen.w, gfx_screen.h, gfx_cur_bpp(), gfx_depth_name(gfx_cur_bpp()),
              gfx_mode_count(), gfx_live_capable() ? "yes" : "no");
    serial_print(m);

    tasks_init();
    idt_init();
    timer_init(TIMER_HZ);
    keyboard_init();
    input_mode_init();

    if (mouse_init()) {
        serial_print("mouse: PS/2 mouse detected.\n");
    } else {
        serial_print("mouse: no PS/2 mouse responded -- using keyboard navigation.\n");
        input_mode_notify_keyboard_nav();
    }

    if (fat16_init(FAT_PARTITION_LBA)) {
        serial_print("fat16: ready.\n");
        run_filesystem_demo();
    } else {
        serial_print("fat16: init FAILED\n");
    }

    audio_init();
    if (sb16_init()) serial_print("sb16: Sound Blaster 16 ready.\n");
    else serial_print("sb16: not present -- audio unavailable.\n");

    wm_init();
    task_exit_hook = wm_task_exited;

    settings_load();
    if (net_init()) serial_print("net: adapter ready.\n");
    else serial_print("net: no supported network adapter.\n");
    settings_apply();

    __asm__ volatile("sti");

    app_launch(APP_PROGMAN);
    app_launch(APP_EXPLORER);
    serial_print("VidBew/OS ready -- Nova desktop, Heroine kernel underneath.\n");

    for (;;) {
        task_reap();
        wm_ui_iteration();
        task_set_ui_idle(1);
        __asm__ volatile("hlt");
        task_set_ui_idle(0);
    }
}
