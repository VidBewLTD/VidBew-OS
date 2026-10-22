/* app_applets.c -- Control Panel applets: Network, Date & Time, System. */

#include "apps.h"
#include "ui.h"
#include "kstd.h"
#include "heap.h"
#include "task.h"
#include "timer.h"
#include "rtc.h"
#include "net.h"
#include "keyboard.h"
#include "settings.h"
#include "sb16.h"
#include "input_mode.h"
#include "bootinfo.h"

/* ---------------------------------------------------------------- Network */

const app_def_t network_def = { "Network", ICON_NETWORK, { 0, 0 }, { 0, 0 }, { 0, 0 } };

#define NW_W 452
#define NW_H 372

typedef struct {
    char f[4][20];            /* ip, mask, gateway, dns as typed */
    int focus;                /* -1 none, 0..3 */
    int dhcp;
    char status[96];
    int pressed;              /* button index held, -1 none */
} nw_t;

static const char *nw_label[4] = { "IP address:", "Subnet mask:", "Default gateway:", "DNS server:" };

static void nw_load_fields(nw_t *n, const net_config_t *c)
{
    const u8 *src[4] = { c->ip, c->mask, c->gw, c->dns };
    for (int i = 0; i < 4; i++)
        if (c->configured || i > 0) net_fmt_ip(src[i], n->f[i]); else kstrlcpy(n->f[i], "0.0.0.0", sizeof(n->f[i]));
}

static void nw_paint(window_t *win, nw_t *n, const net_config_t *c)
{
    surface_t *s = wm_surface(win);
    s_fill(s, 0, 0, NW_W, NW_H, COL_LIGHT_GREY);
    icon_draw(s, 12, 8, 48, ICON_NETWORK);
    if (!net_present()) {
        s_text(s, 72, 14, "No network adapter found.", COL_BLACK, COL_NONE);
        s_text(s, 72, 34, "VidBew/OS supports Intel e1000 and", COL_DARK_GREY, COL_NONE);
        s_text(s, 72, 52, "Realtek RTL8139 network cards.", COL_DARK_GREY, COL_NONE);
    } else {
        char b[80], mac[20];
        net_fmt_mac(c->mac, mac);
        s_text(s, 72, 6, net_nic_name(), COL_BLACK, COL_NONE);
        ksnprintf(b, sizeof(b), "MAC %s", mac);
        s_text(s, 72, 24, b, COL_DARK_GREY, COL_NONE);
        s_text(s, 72, 42, c->link ? "Link: connected" : "Link: no cable / disconnected", c->link ? COL_BLACK : COL_RED, COL_NONE);
        unsigned rx, tx, er;
        net_stats(&rx, &tx, &er);
        ksnprintf(b, sizeof(b), "Packets: %u in, %u out, %u errors", rx, tx, er);
        s_text(s, 72, 60, b, COL_DARK_GREY, COL_NONE);
    }
    int dis = !net_present();
    ui_checkbox(s, 16, 92, "Obtain an address automatically (DHCP)", n->dhcp, dis);
    ui_groupbox(s, 10, 118, NW_W - 20, 148, "Address");
    for (int i = 0; i < 4; i++) {
        s_text(s, 24, 146 + i * 28, nw_label[i], dis ? COL_DARK_GREY : COL_BLACK, COL_NONE);
        ui_textfield(s, 190, 142 + i * 28, 170, n->f[i], n->focus == i, dis || n->dhcp);
    }
    s_text(s, 14, 278, n->status, COL_BLUE, COL_NONE);
    static const char *btn[4] = { "Apply", "Renew", "Ping Gateway", "Close" };
    static const int bw[4] = { 84, 84, 130, 84 };
    int x = 10;
    for (int i = 0; i < 4; i++) {
        ui_button(s, x, NW_H - 42, bw[i], 28, btn[i], n->pressed == i, dis && i < 3);
        x += bw[i] + 10;
    }
    wm_invalidate(win);
}

static int nw_btn_at(int mx, int my)
{
    static const int bw[4] = { 84, 84, 130, 84 };
    int x = 10;
    for (int i = 0; i < 4; i++) {
        if (ui_inside(mx, my, x, NW_H - 42, bw[i], 28)) return i;
        x += bw[i] + 10;
    }
    return -1;
}

static void nw_wait_dhcp(window_t *win, nw_t *n)
{
    ksnprintf(n->status, sizeof(n->status), "Requesting an address from the DHCP server...");
    net_config_t c;
    net_get_config(&c);
    nw_paint(win, n, &c);
    net_request_dhcp();
    for (int i = 0; i < 120; i++) {
        task_sleep(10);
        if (!net_dhcp_busy()) break;
    }
    net_get_config(&c);
    if (c.configured) {
        char a[20];
        net_fmt_ip(c.ip, a);
        ksnprintf(n->status, sizeof(n->status), "Got address %s.", a);
        nw_load_fields(n, &c);
    } else {
        ksnprintf(n->status, sizeof(n->status), "No DHCP server answered.");
    }
}

static void nw_apply(window_t *win, nw_t *n)
{
    if (!net_present()) return;
    if (n->dhcp) {
        g_settings.net_dhcp = 1;
        settings_save();
        nw_wait_dhcp(win, n);
        return;
    }
    u8 ip[4], mask[4], gw[4], dns[4];
    if (!net_parse_ip(n->f[0], ip) || !net_parse_ip(n->f[1], mask) || !net_parse_ip(n->f[2], gw)) {
        ksnprintf(n->status, sizeof(n->status), "Enter valid addresses like 192.168.1.20");
        return;
    }
    if (!net_parse_ip(n->f[3], dns)) memcpy(dns, gw, 4);
    net_set_static(ip, mask, gw, dns);
    g_settings.net_dhcp = 0;
    memcpy(g_settings.ip, ip, 4); memcpy(g_settings.mask, mask, 4);
    memcpy(g_settings.gw, gw, 4); memcpy(g_settings.dns, dns, 4);
    settings_save();
    ksnprintf(n->status, sizeof(n->status), "Static address applied and saved.");
}

void applet_network_main(void *arg)
{
    window_t *win = wm_create("Network", NW_W, NW_H, WF_NO_MIN | WF_NO_MAX, &network_def);
    if (!win) return;
    wm_center(win);
    nw_t *n = (nw_t *)kzalloc(sizeof(nw_t));
    if (!n) { wm_destroy(win); return; }
    net_config_t c;
    net_get_config(&c);
    n->focus = -1;
    n->pressed = -1;
    n->dhcp = c.dhcp || !c.configured ? g_settings.net_dhcp : 0;
    if (!net_present()) n->dhcp = 1;
    nw_load_fields(n, &c);
    nw_paint(win, n, &c);
    unsigned long last = timer_ticks;

    event_t ev;
    for (;;) {
        int got = wm_next_event(win, &ev, 30);
        net_get_config(&c);
        if (!got) {
            if (timer_ticks - last >= 50) {
                last = timer_ticks;
                if (n->dhcp || n->focus < 0) {
                    if (n->dhcp && c.configured) nw_load_fields(n, &c);
                }
                nw_paint(win, n, &c);
            }
            continue;
        }
        switch (ev.type) {
        case EV_CLOSE: kfree(n); wm_destroy(win); return;
        case EV_MOUSE_DOWN:
            if (ev.c != 1) break;
            n->pressed = nw_btn_at(ev.a, ev.b);
            n->focus = -1;
            if (net_present() && ui_inside(ev.a, ev.b, 16, 90, 380, 18)) {
                n->dhcp = !n->dhcp;
                if (n->dhcp) nw_load_fields(n, &c);
            }
            if (!n->dhcp)
                for (int i = 0; i < 4; i++)
                    if (ui_inside(ev.a, ev.b, 190, 142 + i * 28, 170, 22)) n->focus = i;
            break;
        case EV_MOUSE_UP: {
            int b = n->pressed;
            n->pressed = -1;
            if (b >= 0 && nw_btn_at(ev.a, ev.b) == b) {
                if (b == 0) nw_apply(win, n);
                else if (b == 1 && net_present()) { n->dhcp = 1; nw_apply(win, n); }
                else if (b == 2 && net_present()) {
                    if (!c.configured) ksnprintf(n->status, sizeof(n->status), "No address yet -- nothing to ping from.");
                    else {
                        int rtt = 0;
                        char g[20];
                        net_fmt_ip(c.gw, g);
                        ksnprintf(n->status, sizeof(n->status), "Pinging %s...", g);
                        nw_paint(win, n, &c);
                        if (net_ping(c.gw, 1500, &rtt)) ksnprintf(n->status, sizeof(n->status), "Reply from %s: %d ms", g, rtt);
                        else ksnprintf(n->status, sizeof(n->status), "No reply from %s.", g);
                    }
                } else if (b == 3) { kfree(n); wm_destroy(win); return; }
            }
            break;
        }
        case EV_KEY:
            if (ev.a == 27) { kfree(n); wm_destroy(win); return; }
            if (n->focus >= 0 && !n->dhcp) {
                char *f = n->f[n->focus];
                int len = (int)kstrlen(f);
                if ((ev.a >= '0' && ev.a <= '9') || ev.a == '.') { if (len < 15) { f[len] = (char)ev.a; f[len + 1] = 0; } }
                else if (ev.a == '\b') { if (len > 0) f[len - 1] = 0; }
                else if (ev.a == '\t') n->focus = (n->focus + 1) % 4;
                else if (ev.a == '\n') nw_apply(win, n);
            } else if (ev.a == '\n') nw_apply(win, n);
            break;
        default: break;
        }
        nw_paint(win, n, &c);
    }
}

/* ---------------------------------------------------------------- Date & Time */

const app_def_t datetime_def = { "Date & Time", ICON_CALENDAR, { 0, 0 }, { 0, 0 }, { 0, 0 } };

#define DT_W 392
#define DT_H 262

static const char *month_names[12] = { "January","February","March","April","May","June","July","August","September","October","November","December" };

static void dt_paint(window_t *win, const int v[6], int editing, int pressed, const char *msg)
{
    surface_t *s = wm_surface(win);
    s_fill(s, 0, 0, DT_W, DT_H, COL_LIGHT_GREY);
    icon_draw(s, 12, 8, 48, ICON_CALENDAR);
    char b[64];
    ksnprintf(b, sizeof(b), "%04d-%02d-%02d  %02d:%02d:%02d", v[0], v[1], v[2], v[3], v[4], v[5]);
    s_text(s, 76, 12, b, COL_BLACK, COL_NONE);
    s_text(s, 76, 32, month_names[(v[1] - 1 + 12) % 12], COL_DARK_GREY, COL_NONE);
    s_text(s, 76, 50, editing ? "Edited -- press Set to apply" : "Showing the live clock", COL_DARK_GREY, COL_NONE);
    static const char *lbl[6] = { "Year", "Month", "Day", "Hour", "Minute", "Second" };
    for (int i = 0; i < 6; i++) {
        int col = i / 3, row = i % 3;
        int x = 14 + col * 188, y = 84 + row * 36;
        s_text(s, x, y + 5, lbl[i], COL_BLACK, COL_NONE);
        ui_button(s, x + 76, y, 26, 26, "-", pressed == i * 2, 0);
        ui_button(s, x + 108, y, 26, 26, "+", pressed == i * 2 + 1, 0);
    }
    s_text(s, 14, 196, msg, COL_BLUE, COL_NONE);
    ui_button(s, 10, DT_H - 40, 90, 28, "Set", pressed == 12, !editing);
    ui_button(s, DT_W - 94, DT_H - 40, 84, 28, "Close", pressed == 13, 0);
    wm_invalidate(win);
}

static int dt_btn_at(int mx, int my)
{
    for (int i = 0; i < 6; i++) {
        int col = i / 3, row = i % 3, x = 14 + col * 188, y = 84 + row * 36;
        if (ui_inside(mx, my, x + 76, y, 26, 26)) return i * 2;
        if (ui_inside(mx, my, x + 108, y, 26, 26)) return i * 2 + 1;
    }
    if (ui_inside(mx, my, 10, DT_H - 40, 90, 28)) return 12;
    if (ui_inside(mx, my, DT_W - 94, DT_H - 40, 84, 28)) return 13;
    return -1;
}

static void dt_clamp(int v[6])
{
    if (v[0] < 2000) v[0] = 2000;
    if (v[0] > 2099) v[0] = 2099;
    if (v[1] < 1) v[1] = 12;
    if (v[1] > 12) v[1] = 1;
    int dim = rtc_days_in_month(v[0], v[1]);
    if (v[2] < 1) v[2] = dim;
    if (v[2] > dim) v[2] = 1;
    if (v[3] < 0) v[3] = 23;
    if (v[3] > 23) v[3] = 0;
    if (v[4] < 0) v[4] = 59;
    if (v[4] > 59) v[4] = 0;
    if (v[5] < 0) v[5] = 59;
    if (v[5] > 59) v[5] = 0;
}

void applet_datetime_main(void *arg)
{
    window_t *win = wm_create("Date & Time", DT_W, DT_H, WF_NO_MIN | WF_NO_MAX, &datetime_def);
    if (!win) return;
    wm_center(win);
    int v[6], editing = 0, pressed = -1;
    char msg[64] = "";
    rtc_read_date(&v[0], &v[1], &v[2]);
    rtc_read_time(&v[3], &v[4], &v[5]);
    dt_paint(win, v, editing, pressed, msg);

    event_t ev;
    for (;;) {
        int got = wm_next_event(win, &ev, 25);
        if (!got) {
            if (!editing) { rtc_read_date(&v[0], &v[1], &v[2]); rtc_read_time(&v[3], &v[4], &v[5]); dt_paint(win, v, editing, pressed, msg); }
            continue;
        }
        switch (ev.type) {
        case EV_CLOSE: wm_destroy(win); return;
        case EV_KEY: if (ev.a == 27) { wm_destroy(win); return; } break;
        case EV_MOUSE_DOWN: if (ev.c == 1) pressed = dt_btn_at(ev.a, ev.b); break;
        case EV_MOUSE_UP: {
            int b = pressed;
            pressed = -1;
            if (b >= 0 && dt_btn_at(ev.a, ev.b) == b) {
                if (b < 12) {
                    editing = 1;
                    v[b / 2] += (b & 1) ? 1 : -1;
                    dt_clamp(v);
                } else if (b == 12 && editing) {
                    rtc_set_date(v[0], v[1], v[2]);
                    rtc_set_time(v[3], v[4], v[5]);
                    editing = 0;
                    kstrlcpy(msg, "Clock set.", sizeof(msg));
                } else if (b == 13) { wm_destroy(win); return; }
            }
            break;
        }
        default: break;
        }
        dt_paint(win, v, editing, pressed, msg);
    }
}

/* ---------------------------------------------------------------- System */

const app_def_t system_def = { "System", ICON_SYSTEM, { 0, 0 }, { 0, 0 }, { 0, 0 } };

#define SY_W 440
#define SY_H 350

static void cpu_vendor(char *out)
{
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    memcpy(out, &b, 4); memcpy(out + 4, &d, 4); memcpy(out + 8, &c, 4);
    out[12] = 0;
}

static void cpu_brand(char *out, int cap)
{
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u));
    out[0] = 0;
    if (a < 0x80000004u) return;
    char raw[49];
    for (unsigned i = 0; i < 3; i++) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000002u + i));
        memcpy(raw + i * 16, &a, 4); memcpy(raw + i * 16 + 4, &b, 4);
        memcpy(raw + i * 16 + 8, &c, 4); memcpy(raw + i * 16 + 12, &d, 4);
    }
    raw[48] = 0;
    const char *p = raw;
    while (*p == ' ') p++;
    kstrlcpy(out, p, (unsigned)cap);
}

static void sy_paint(window_t *win, int pressed)
{
    surface_t *s = wm_surface(win);
    s_fill(s, 0, 0, SY_W, SY_H, COL_LIGHT_GREY);
    icon_draw(s, 12, 8, 56, ICON_LOGO);
    s_text(s, 84, 12, "VidBew/OS 1.0", COL_BLACK, COL_NONE);
    s_text(s, 84, 32, "Nova desktop - Heroine kernel", COL_DARK_GREY, COL_NONE);
    s_text(s, 84, 50, "Written from scratch", COL_DARK_GREY, COL_NONE);
    s_hline(s, 10, 76, SY_W - 20, COL_DARK_GREY);
    s_hline(s, 10, 77, SY_W - 20, COL_WHITE);

    char vendor[16], brand[52], b[100];
    cpu_vendor(vendor);
    cpu_brand(brand, sizeof(brand));
    int y = 88;
    s_text(s, 14, y, "Processor:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, brand[0] ? brand : vendor, COL_BLACK, COL_NONE);
    y += 20;
    unsigned ext = BOOT_INFO->ext_kb;
    ksnprintf(b, sizeof(b), "%u MB (BIOS reports)", ext / 1024 + 1);
    s_text(s, 14, y, "Memory:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, b, COL_BLACK, COL_NONE);
    y += 20;
    unsigned used, freeb, total;
    heap_stats(&used, &freeb, &total);
    ksnprintf(b, sizeof(b), "%u KB of %u KB used", used / 1024, total / 1024);
    s_text(s, 14, y, "Heap:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, b, COL_BLACK, COL_NONE);
    ui_progress(s, 120, y + 18, 220, 12, (int)(used / (total / 100 + 1)));
    y += 38;
    ksnprintf(b, sizeof(b), "%dx%d, %s", SCREEN_WIDTH, SCREEN_HEIGHT, gfx_depth_name(gfx_cur_bpp()));
    s_text(s, 14, y, "Display:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, b, COL_BLACK, COL_NONE);
    y += 20;
    s_text(s, 14, y, "Network:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, net_present() ? net_nic_name() : "none", COL_BLACK, COL_NONE);
    y += 20;
    s_text(s, 14, y, "Sound:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, sb16_present() ? "Sound Blaster 16" : "none", COL_BLACK, COL_NONE);
    y += 20;
    ksnprintf(b, sizeof(b), "%d running, %d windows", task_count(), wm_window_count());
    s_text(s, 14, y, "Tasks:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, b, COL_BLACK, COL_NONE);
    y += 20;
    unsigned up = (unsigned)(timer_ticks / 100);
    ksnprintf(b, sizeof(b), "%u:%02u:%02u", up / 3600, (up / 60) % 60, up % 60);
    s_text(s, 14, y, "Uptime:", COL_BLACK, COL_NONE);
    s_text(s, 120, y, b, COL_BLACK, COL_NONE);
    ui_button(s, SY_W - 94, SY_H - 40, 84, 28, "Close", pressed, 0);
    wm_invalidate(win);
}

void applet_system_main(void *arg)
{
    window_t *win = wm_create("System", SY_W, SY_H, WF_NO_MIN | WF_NO_MAX, &system_def);
    if (!win) return;
    wm_center(win);
    int pressed = 0;
    sy_paint(win, pressed);
    event_t ev;
    for (;;) {
        int got = wm_next_event(win, &ev, 50);
        if (got) {
            if (ev.type == EV_CLOSE || (ev.type == EV_KEY && (ev.a == 27 || ev.a == '\n'))) { wm_destroy(win); return; }
            if (ev.type == EV_MOUSE_DOWN && ui_inside(ev.a, ev.b, SY_W - 94, SY_H - 40, 84, 28)) pressed = 1;
            if (ev.type == EV_MOUSE_UP) {
                if (pressed && ui_inside(ev.a, ev.b, SY_W - 94, SY_H - 40, 84, 28)) { wm_destroy(win); return; }
                pressed = 0;
            }
        }
        sy_paint(win, pressed);
    }
}
