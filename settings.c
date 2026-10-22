/* settings.c -- load/save/apply the persistent settings sector. */

#include "settings.h"
#include "fat16.h"
#include "kstd.h"
#include "wm.h"
#include "mouse.h"
#include "sb16.h"
#include "net.h"
#include "serial.h"

settings_t g_settings;

static void defaults(void)
{
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.magic = 0x534F4256;
    g_settings.version = 1;
    g_settings.disp_w = 800;
    g_settings.disp_h = 600;
    g_settings.disp_bpp = 8;
    g_settings.scheme = 0;
    g_settings.dbl_ticks = 40;
    g_settings.mouse_pct = 100;
    g_settings.volume = 80;
    g_settings.net_dhcp = 1;
    g_settings.boot_chime = 1;
    g_settings.show_bootscreen = 1;
}

void settings_load(void)
{
    unsigned char sec[512];
    defaults();
    if (!fat16_raw_read(SETTINGS_LBA, sec)) { serial_print("settings: sector unreadable, using defaults\n"); return; }
    settings_t *s = (settings_t *)sec;
    if (s->magic != 0x534F4256 || s->version != 1) { serial_print("settings: none saved yet, using defaults\n"); return; }
    memcpy(&g_settings, s, sizeof(settings_t));
    if (g_settings.dbl_ticks < 10 || g_settings.dbl_ticks > 100) g_settings.dbl_ticks = 40;
    if (g_settings.mouse_pct < 50 || g_settings.mouse_pct > 300) g_settings.mouse_pct = 100;
    if (g_settings.volume > 100) g_settings.volume = 80;
    if (g_settings.scheme >= wm_scheme_count()) g_settings.scheme = 0;
    serial_print("settings: loaded\n");
}

int settings_save(void)
{
    unsigned char sec[512];
    memset(sec, 0, sizeof(sec));
    g_settings.magic = 0x534F4256;
    g_settings.version = 1;
    memcpy(sec, &g_settings, sizeof(settings_t));
    return fat16_raw_write(SETTINGS_LBA, sec);
}

void settings_apply(void)
{
    wm_set_scheme(g_settings.scheme);
    wm_set_dblclick_ticks(g_settings.dbl_ticks);
    mouse_set_speed(g_settings.mouse_pct);
    sb16_set_volume(g_settings.volume);
    if (!g_settings.net_dhcp && net_present())
        net_set_static(g_settings.ip, g_settings.mask, g_settings.gw, g_settings.dns);
}
