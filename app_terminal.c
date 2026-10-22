/* app_terminal.c -- the Heroine command line, in a window.
 *
 * "Nova" is the graphical shell you see; "Heroine" is the command core
 * underneath it. The command set is deliberately its own vocabulary rather
 * than a reskin of ls/cat/echo: inventory, inspect, scribe, proclaim,
 * chronicle, vigil, horizon, essence, guide, resonate -- and, with
 * networking and the window manager, portal, legion, vista, tether, summon,
 * beacon, seek, anchor, rebirth, farewell.
 *
 * Each Terminal window is its own task with its own scrollback, so a long
 * command (a ping, a sound) only occupies that window. */

#include "apps.h"
#include "ui.h"
#include "fat16.h"
#include "rtc.h"
#include "timer.h"
#include "serial.h"
#include "audio.h"
#include "sb16.h"
#include "net.h"
#include "task.h"
#include "heap.h"
#include "kstd.h"
#include "keyboard.h"
#include "settings.h"
#include "sync.h"

#define T_COLS_MAX 160
#define T_LINES    200
#define T_INPUT    120
#define T_HIST     8

enum { T_COPY = 1, T_PASTE, T_CLEAR, T_BOTTOM };

static const menu_item_t edit_items[] = {
    { "Copy Input Line", T_COPY,  0, 0 },
    { "Paste",           T_PASTE, 0, 0 },
    { "Clear Screen",    T_CLEAR, 0, 0 },
};
static const menu_item_t view_items[] = {
    { "Scroll to Bottom", T_BOTTOM, 0, "End" },
};
const app_def_t terminal_def = { "Terminal", ICON_MONITOR, { 0, 0 }, { 3, edit_items }, { 1, view_items } };

typedef struct {
    window_t *win;
    char (*lines)[T_COLS_MAX + 1];
    unsigned char *line_color;
    int head, count, col, scroll;
    int cols, rows;
    unsigned char color;
    char input[T_INPUT + 1];
    int input_len;
    char hist[T_HIST][T_INPUT + 1];
    int hist_n, hist_pos;
    int dragging_scroll;
    int tid;
} term_t;

#define C_TEXT   COL_LIGHT_GREEN
#define C_PROMPT COL_LIGHT_CYAN
#define C_ERROR  COL_LIGHT_RED
#define C_HEAD   COL_YELLOW

static const char *PROMPT = "heroine> ";

/* ---------------------------------------------------------------- buffer */

static void new_line(term_t *t)
{
    t->head = (t->head + 1) % T_LINES;
    t->lines[t->head][0] = 0;
    t->line_color[t->head] = t->color;
    t->col = 0;
    if (t->count < T_LINES) t->count++;
}

static void putc_t(term_t *t, char c)
{
    if (c == '\n') { new_line(t); return; }
    if (c == '\r') return;
    if (c == '\t') c = ' ';
    if (t->col >= t->cols) new_line(t);
    t->lines[t->head][t->col++] = c;
    t->lines[t->head][t->col] = 0;
}

static void paint(term_t *t)
{
    window_t *win = t->win;
    surface_t *s = wm_surface(win);
    int cw, ch;
    wm_client_size(win, &cw, &ch);
    if (s->w < cw || s->h < ch) return;
    t->cols = (cw - 8 - UI_SCROLL_W) / FONT_WIDTH;
    if (t->cols > T_COLS_MAX) t->cols = T_COLS_MAX;
    if (t->cols < 20) t->cols = 20;
    t->rows = (ch - 4) / FONT_HEIGHT;

    s_fill(s, 0, 0, cw, ch, COL_BLACK);
    int max_scroll = t->count > t->rows ? t->count - t->rows : 0;
    if (t->scroll > max_scroll) t->scroll = max_scroll;
    for (int r = 0; r < t->rows; r++) {
        int back = (t->rows - 1 - r) + t->scroll;          /* lines above the newest */
        if (back >= t->count) continue;
        int idx = ((t->head - back) % T_LINES + T_LINES) % T_LINES;
        s_text_n(s, 4, 2 + r * FONT_HEIGHT, t->lines[idx], t->cols, t->line_color[idx], COL_NONE);
    }
    if (t->scroll == 0 && wm_is_focused(win)) {            /* block cursor at the end of the live line */
        int r = t->rows - 1;
        s_fill(s, 4 + t->col * FONT_WIDTH, 2 + r * FONT_HEIGHT + 12, FONT_WIDTH, 3, C_PROMPT);
    }
    ui_vscroll(s, cw - UI_SCROLL_W, 0, ch, t->count > t->rows ? t->count : t->rows, t->rows,
               max_scroll - t->scroll);
    wm_invalidate(win);
}

static void tprint_c(term_t *t, int color, const char *str)
{
    serial_print("[TERM] ");
    serial_print(str);
    unsigned char saved = t->color;
    t->color = (unsigned char)color;
    if (t->col == 0 && t->lines[t->head][0] == 0) t->line_color[t->head] = (unsigned char)color;
    for (const char *p = str; *p; p++) {
        putc_t(t, *p);
        if (*p == '\n') t->line_color[t->head] = (unsigned char)color;
    }
    t->color = saved;
    t->scroll = 0;
    paint(t);
}

static void tprint(term_t *t, const char *str) { tprint_c(t, C_TEXT, str); }

static void tprintf(term_t *t, int color, const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    tprint_c(t, color, buf);
}

static void clear_screen(term_t *t)
{
    t->head = 0;
    t->count = 1;
    t->col = 0;
    t->lines[0][0] = 0;
    t->scroll = 0;
}

static void prompt(term_t *t)
{
    tprint_c(t, C_PROMPT, PROMPT);
}

/* ---------------------------------------------------------------- commands */

static void cmd_guide(term_t *t)
{
    tprint_c(t, C_HEAD, "Heroine command guide:\n");
    tprint(t, " guide            - this list\n");
    tprint(t, " inventory        - list files on disk\n");
    tprint(t, " inspect <file>   - show a file's contents\n");
    tprint(t, " scribe <f> <txt> - write txt to file f\n");
    tprint(t, " proclaim <text>  - print text back\n");
    tprint(t, " chronicle        - version + uptime\n");
    tprint(t, " vigil            - current date and time\n");
    tprint(t, " horizon          - clear the screen\n");
    tprint(t, " essence          - what Heroine is\n");
    tprint(t, " resonate <file>  - play a WAV file\n");
    tprint(t, " portal <app>     - open a window (explorer, terminal,\n");
    tprint(t, "                    control, tasks, clock, keyboard,\n");
    tprint(t, "                    network, display, help, about)\n");
    tprint(t, " legion           - list running tasks\n");
    tprint(t, " vista            - display mode and available modes\n");
    tprint(t, " tether           - network status\n");
    tprint(t, " summon           - request an address (DHCP)\n");
    tprint(t, " beacon <host>    - ping an address or name\n");
    tprint(t, " seek <name>      - look up a hostname (DNS)\n");
    tprint(t, " anchor <ip> <mask> <gw> [dns] - set a static address\n");
    tprint(t, " rebirth          - restart\n");
    tprint(t, " farewell         - shut down\n");
    tprint(t, " shatter          - crash THIS terminal on purpose;\n");
    tprint(t, "                    the rest of the OS keeps running\n");
}

static void cmd_inventory(term_t *t)
{
    static fat16_entry_t ents[64];
    /* static is fine here: the buffer is only touched while this task holds the fs lock inside read_dir,
     * and we copy out before printing; two terminals racing would at worst interleave listings. */
    fat16_entry_t *local = (fat16_entry_t *)kmalloc(sizeof(fat16_entry_t) * 64);
    (void)ents;
    if (!local) { tprint_c(t, C_ERROR, "Out of memory.\n"); return; }
    int n = fat16_read_dir(local, 64);
    if (n == 0) tprint(t, "  (nothing here)\n");
    for (int i = 0; i < n; i++)
        tprintf(t, C_TEXT, "  %s  (%u bytes)\n", local[i].name, local[i].size);
    kfree(local);
}

static void cmd_inspect(term_t *t, const char *arg)
{
    if (!*arg) { tprint(t, "inspect needs a filename, e.g. inspect HELLO.TXT\n"); return; }
    unsigned char *buf = (unsigned char *)kmalloc(2048);
    if (!buf) { tprint_c(t, C_ERROR, "Out of memory.\n"); return; }
    unsigned size = 0;
    if (fat16_read_file(arg, buf, 2047, &size)) {
        buf[size] = 0;
        tprint(t, (const char *)buf);
        if (size == 0 || buf[size - 1] != '\n') tprint(t, "\n");
    } else {
        tprint_c(t, C_ERROR, "Can't find that file.\n");
    }
    kfree(buf);
}

static void cmd_scribe(term_t *t, const char *arg)
{
    char name[32];
    int i = 0;
    while (arg[i] && arg[i] != ' ' && i < 31) { name[i] = arg[i]; i++; }
    name[i] = 0;
    while (arg[i] == ' ') i++;
    const char *text = &arg[i];
    if (name[0] == 0 || text[0] == 0) {
        tprint(t, "scribe needs a filename and text, e.g. scribe NOTE.TXT hello\n");
        return;
    }
    if (fat16_write_file(name, (const unsigned char *)text, kstrlen(text))) tprint(t, "Written.\n");
    else tprint_c(t, C_ERROR, "Couldn't write that file.\n");
}

static void cmd_chronicle(term_t *t)
{
    tprint(t, "VidBew/OS 1.0 -- Nova desktop, Interface Manager\n");
    tprint(t, "Heroine kernel -- command core\n");
    tprintf(t, C_TEXT, "Uptime: %u seconds\n", (unsigned)(timer_ticks / 100));
    unsigned used, freeb, total;
    heap_stats(&used, &freeb, &total);
    tprintf(t, C_TEXT, "Memory: %u KB in use of %u KB\n", used / 1024, total / 1024);
}

static void cmd_vigil(term_t *t)
{
    int h, m, s, y, mo, d;
    rtc_read_time(&h, &m, &s);
    rtc_read_date(&y, &mo, &d);
    tprintf(t, C_TEXT, "%04d-%02d-%02d  %02d:%02d:%02d\n", y, mo, d, h, m, s);
}

static void cmd_essence(term_t *t)
{
    tprint(t, "Heroine is the command core beneath Nova's desktop --\n");
    tprint(t, "the same kernel, reached with words instead of clicks.\n");
}

static void cmd_resonate(term_t *t, const char *arg)
{
    if (!*arg) { tprint(t, "resonate needs a filename, e.g. resonate CHIME.WAV\n"); return; }
    if (!sb16_present()) { tprint_c(t, C_ERROR, "No Sound Blaster 16 detected.\n"); return; }
    wav_info_t wav;
    unsigned pcm_len = 0;
    tprintf(t, C_TEXT, "Playing %s...\n", arg);
    int r = audio_play_file(arg, &wav, &pcm_len);
    if (r == 1) tprint_c(t, C_ERROR, "Can't find that file.\n");
    else if (r == 2) tprint_c(t, C_ERROR, "That doesn't look like a WAV file Heroine understands.\n");
    else if (r == 0) tprintf(t, C_TEXT, "Done (%uHz, %uch, %u-bit).\n", wav.sample_rate, wav.channels, wav.bits_per_sample);
}

static void cmd_portal(term_t *t, const char *arg)
{
    static const struct { const char *name; int app; } map[] = {
        { "explorer", APP_EXPLORER }, { "terminal", APP_TERMINAL }, { "control", APP_CONTROL },
        { "tasks", APP_TASKMAN }, { "clock", APP_CLOCK }, { "keyboard", APP_OSK },
        { "network", APP_NETWORK }, { "display", APP_DISPLAY }, { "help", APP_HELP },
        { "about", APP_ABOUT }, { "programs", APP_PROGMAN },
        { "calc", APP_CALC }, { "calendar", APP_CALENDAR }, { "alarm", APP_ALARM },
        { "paint", APP_PAINT }, { "images", APP_VIEWER },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (kstricmp(arg, map[i].name) == 0) { app_launch(map[i].app); tprintf(t, C_TEXT, "Opening %s.\n", app_name(map[i].app)); return; }
    tprint(t, "portal opens: explorer, terminal, control, tasks, clock, keyboard,\n");
    tprint(t, "network, display, help, about, programs, calc, calendar, alarm,\n");
    tprint(t, "paint, images\n");
}

static void cmd_legion(term_t *t)
{
    tprint_c(t, C_HEAD, "  ID  Task              State     CPU ticks\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        int st = task_state(i);
        if (st == TASK_FREE) continue;
        tprintf(t, C_TEXT, "  %2d  %-16s  %-8s  %u\n", i, task_name(i),
                st == TASK_SLEEPING ? "waiting" : "running", (unsigned)task_cpu_ticks(i));
    }
    tprintf(t, C_TEXT, "%d tasks, %d windows.\n", task_count(), wm_window_count());
}

static void cmd_vista(term_t *t)
{
    tprintf(t, C_TEXT, "Display: %dx%d, %s\n", SCREEN_WIDTH, SCREEN_HEIGHT, gfx_depth_name(gfx_cur_bpp()));
    tprintf(t, C_TEXT, "Live mode switching: %s\n", gfx_live_capable() ? "yes (Control Panel > Display)" : "no (a restart applies a new mode)");
    tprint_c(t, C_HEAD, "Modes the video BIOS offers:\n");
    int lw = -1, lh = -1;
    char line[100];
    line[0] = 0;
    for (int i = 0; i < gfx_mode_count(); i++) {
        const vbe_mode_t *m = gfx_mode_at(i);
        if (m->w != lw || m->h != lh) {
            if (line[0]) { kstrlcat(line, "\n", sizeof(line)); tprint(t, line); }
            ksnprintf(line, sizeof(line), "  %dx%d:", m->w, m->h);
            lw = m->w; lh = m->h;
        }
        char b[12];
        ksnprintf(b, sizeof(b), " %d", m->bpp);
        kstrlcat(line, b, sizeof(line));
    }
    if (line[0]) { kstrlcat(line, "  (bits per pixel)\n", sizeof(line)); tprint(t, line); }
}

static void cmd_tether(term_t *t)
{
    if (!net_present()) { tprint_c(t, C_ERROR, "No network adapter found.\n"); return; }
    net_config_t c;
    net_get_config(&c);
    char a[20], b[20], g[20], d[20], mac[20];
    net_fmt_ip(c.ip, a); net_fmt_ip(c.mask, b); net_fmt_ip(c.gw, g); net_fmt_ip(c.dns, d);
    net_fmt_mac(c.mac, mac);
    unsigned rx, tx, er;
    net_stats(&rx, &tx, &er);
    tprintf(t, C_TEXT, "Adapter: %s   MAC %s\n", net_nic_name(), mac);
    tprintf(t, C_TEXT, "Link: %s\n", c.link ? "up" : "down");
    if (c.configured) {
        tprintf(t, C_TEXT, "Address: %s / %s (%s)\n", a, b, c.dhcp ? "DHCP" : "static");
        tprintf(t, C_TEXT, "Gateway: %s   DNS: %s\n", g, d);
    } else {
        tprint(t, "No address yet. Try 'summon'.\n");
    }
    tprintf(t, C_TEXT, "Packets: %u received, %u sent, %u errors\n", rx, tx, er);
}

static void cmd_summon(term_t *t)
{
    if (!net_present()) { tprint_c(t, C_ERROR, "No network adapter found.\n"); return; }
    tprint(t, "Requesting an address...\n");
    g_settings.net_dhcp = 1;
    net_request_dhcp();
    for (int i = 0; i < 100; i++) {
        task_sleep(10);
        if (!net_dhcp_busy()) break;
    }
    net_config_t c;
    net_get_config(&c);
    if (c.configured) {
        char a[20], g[20];
        net_fmt_ip(c.ip, a);
        net_fmt_ip(c.gw, g);
        tprintf(t, C_TEXT, "Got %s (gateway %s).\n", a, g);
    } else {
        tprint_c(t, C_ERROR, "No DHCP server answered.\n");
    }
}

static int resolve_host(term_t *t, const char *host, u8 ip[4])
{
    if (net_parse_ip(host, ip)) return 1;
    tprintf(t, C_TEXT, "Looking up %s...\n", host);
    if (net_resolve(host, ip, 3000)) return 1;
    tprint_c(t, C_ERROR, "Couldn't resolve that name.\n");
    return 0;
}

static void cmd_beacon(term_t *t, const char *arg)
{
    if (!net_present()) { tprint_c(t, C_ERROR, "No network adapter found.\n"); return; }
    if (!*arg) { tprint(t, "beacon needs an address or name, e.g. beacon 10.0.2.2\n"); return; }
    u8 ip[4];
    if (!resolve_host(t, arg, ip)) return;
    char a[20];
    net_fmt_ip(ip, a);
    int ok = 0;
    for (int i = 0; i < 4; i++) {
        int rtt = 0;
        if (net_ping(ip, 1500, &rtt)) {
            ok++;
            if (rtt < 1) tprintf(t, C_TEXT, "Reply from %s: time<1 ms\n", a);
            else tprintf(t, C_TEXT, "Reply from %s: time=%d ms\n", a, rtt);
        }
        else tprint_c(t, C_ERROR, "Request timed out.\n");
        if (i < 3) task_sleep(50);
    }
    tprintf(t, C_TEXT, "%s: 4 sent, %d received.\n", a, ok);
}

static void cmd_seek(term_t *t, const char *arg)
{
    if (!net_present()) { tprint_c(t, C_ERROR, "No network adapter found.\n"); return; }
    if (!*arg) { tprint(t, "seek needs a hostname, e.g. seek example.com\n"); return; }
    u8 ip[4];
    if (net_resolve(arg, ip, 3000)) {
        char a[20];
        net_fmt_ip(ip, a);
        tprintf(t, C_TEXT, "%s is at %s\n", arg, a);
    } else {
        tprint_c(t, C_ERROR, "No answer from the DNS server.\n");
    }
}

static void cmd_anchor(term_t *t, const char *arg)
{
    if (!net_present()) { tprint_c(t, C_ERROR, "No network adapter found.\n"); return; }
    char tok[4][24];
    int n = 0;
    const char *p = arg;
    while (*p && n < 4) {
        while (*p == ' ') p++;
        int k = 0;
        while (*p && *p != ' ' && k < 23) tok[n][k++] = *p++;
        tok[n][k] = 0;
        if (k) n++;
    }
    u8 ip[4], mask[4], gw[4], dns[4];
    if (n < 3 || !net_parse_ip(tok[0], ip) || !net_parse_ip(tok[1], mask) || !net_parse_ip(tok[2], gw)) {
        tprint(t, "anchor <ip> <mask> <gateway> [dns], e.g.\n anchor 10.0.2.20 255.255.255.0 10.0.2.2 10.0.2.3\n");
        return;
    }
    if (n < 4 || !net_parse_ip(tok[3], dns)) memcpy(dns, gw, 4);
    net_set_static(ip, mask, gw, dns);
    g_settings.net_dhcp = 0;
    memcpy(g_settings.ip, ip, 4); memcpy(g_settings.mask, mask, 4);
    memcpy(g_settings.gw, gw, 4); memcpy(g_settings.dns, dns, 4);
    settings_save();
    tprint(t, "Static address set and saved.\n");
}

typedef struct { const char *name; } cmdname_t;
static const char *const cmd_names[] = {
    "guide", "inventory", "inspect", "scribe", "proclaim", "chronicle", "vigil", "horizon",
    "essence", "resonate", "portal", "legion", "vista", "tether", "summon", "beacon", "seek",
    "anchor", "rebirth", "farewell", "shatter",
};
#define NCMDS ((int)(sizeof(cmd_names) / sizeof(cmd_names[0])))

static void execute(term_t *t, const char *line)
{
    char cmd[32];
    int i = 0;
    while (line[i] == ' ') i++;
    line += i;
    i = 0;
    while (line[i] && line[i] != ' ' && i < 31) { cmd[i] = line[i]; i++; }
    cmd[i] = 0;
    while (line[i] == ' ') i++;
    const char *rest = &line[i];

    if (cmd[0] == 0) return;
    else if (!kstrcmp(cmd, "guide")) cmd_guide(t);
    else if (!kstrcmp(cmd, "inventory")) cmd_inventory(t);
    else if (!kstrcmp(cmd, "inspect")) cmd_inspect(t, rest);
    else if (!kstrcmp(cmd, "scribe")) cmd_scribe(t, rest);
    else if (!kstrcmp(cmd, "proclaim")) { tprint(t, rest); tprint(t, "\n"); }
    else if (!kstrcmp(cmd, "chronicle")) cmd_chronicle(t);
    else if (!kstrcmp(cmd, "vigil")) cmd_vigil(t);
    else if (!kstrcmp(cmd, "horizon")) clear_screen(t);
    else if (!kstrcmp(cmd, "essence")) cmd_essence(t);
    else if (!kstrcmp(cmd, "resonate")) cmd_resonate(t, rest);
    else if (!kstrcmp(cmd, "portal")) cmd_portal(t, rest);
    else if (!kstrcmp(cmd, "legion")) cmd_legion(t);
    else if (!kstrcmp(cmd, "vista")) cmd_vista(t);
    else if (!kstrcmp(cmd, "tether")) cmd_tether(t);
    else if (!kstrcmp(cmd, "summon")) cmd_summon(t);
    else if (!kstrcmp(cmd, "beacon")) cmd_beacon(t, rest);
    else if (!kstrcmp(cmd, "seek")) cmd_seek(t, rest);
    else if (!kstrcmp(cmd, "anchor")) cmd_anchor(t, rest);
    else if (!kstrcmp(cmd, "rebirth")) { tprint(t, "Restarting...\n"); wm_post_command(CMD_RESTART_NOW); }
    else if (!kstrcmp(cmd, "farewell")) { tprint(t, "Goodbye.\n"); wm_post_command(CMD_SHUTDOWN_NOW); }
    else if (!kstrcmp(cmd, "shatter")) {
        tprint_c(t, C_ERROR, "Executing an illegal instruction in this terminal...\n");
        __asm__ volatile("ud2");     /* a CPU exception: only this task dies, its window is cleaned up */
    }
    else tprint_c(t, C_ERROR, "Not a word Heroine knows. Try 'guide'.\n");
}

/* ---------------------------------------------------------------- line editing */

static void set_input(term_t *t, const char *text)
{
    while (t->input_len > 0) {                 /* erase what is on screen */
        t->input_len--;
        if (t->col > 0) t->col--;
        t->lines[t->head][t->col] = 0;
    }
    for (const char *p = text; *p && t->input_len < T_INPUT; p++) {
        t->input[t->input_len++] = *p;
        putc_t(t, *p);
    }
    t->input[t->input_len] = 0;
}

static void tab_complete(term_t *t)
{
    for (int i = 0; i < t->input_len; i++) if (t->input[i] == ' ') return;
    int hits = 0, last = -1;
    for (int i = 0; i < NCMDS; i++)
        if (!kstrncmp(cmd_names[i], t->input, (unsigned)t->input_len)) { hits++; last = i; }
    if (hits == 1) { set_input(t, cmd_names[last]); putc_t(t, ' '); t->input[t->input_len++] = ' '; t->input[t->input_len] = 0; }
    else if (hits > 1) {
        char save[T_INPUT + 1];
        kstrlcpy(save, t->input, sizeof(save));
        tprint(t, "\n");
        for (int i = 0; i < NCMDS; i++)
            if (!kstrncmp(cmd_names[i], save, (unsigned)kstrlen(save))) { tprint(t, cmd_names[i]); tprint(t, "  "); }
        tprint(t, "\n");
        prompt(t);
        t->input_len = 0;
        set_input(t, save);
    }
}

static void handle_key(term_t *t, int code)
{
    if (code == '\n') {
        tprint(t, "\n");
        t->input[t->input_len] = 0;
        if (t->input_len) {
            if (t->hist_n == T_HIST) { memmove(t->hist[0], t->hist[1], (T_HIST - 1) * (T_INPUT + 1)); t->hist_n--; }
            kstrlcpy(t->hist[t->hist_n++], t->input, T_INPUT + 1);
        }
        t->hist_pos = t->hist_n;
        char line[T_INPUT + 1];
        kstrlcpy(line, t->input, sizeof(line));
        t->input_len = 0;
        t->input[0] = 0;
        execute(t, line);
        prompt(t);
    } else if (code == '\b') {
        if (t->input_len > 0) {
            t->input_len--;
            t->input[t->input_len] = 0;
            if (t->col > 0) { t->col--; t->lines[t->head][t->col] = 0; }
        }
    } else if (code == '\t') {
        tab_complete(t);
    } else if (code == KEY_UP) {
        if (t->hist_pos > 0) { t->hist_pos--; set_input(t, t->hist[t->hist_pos]); }
    } else if (code == KEY_DOWN) {
        if (t->hist_pos < t->hist_n - 1) { t->hist_pos++; set_input(t, t->hist[t->hist_pos]); }
        else { t->hist_pos = t->hist_n; set_input(t, ""); }
    } else if (code == KEY_PGUP) {
        t->scroll += t->rows - 1;
    } else if (code == KEY_PGDN) {
        t->scroll -= t->rows - 1;
        if (t->scroll < 0) t->scroll = 0;
    } else if (code == KEY_END) {
        t->scroll = 0;
    } else if (code == KEY_HOME) {
        t->scroll = t->count;
    } else if (code >= 32 && code < 127 && t->input_len < T_INPUT) {
        t->input[t->input_len++] = (char)code;
        t->input[t->input_len] = 0;
        putc_t(t, (char)code);
        t->scroll = 0;
    }
}

void app_terminal_main(void *arg)
{
    term_t *t = (term_t *)kzalloc(sizeof(term_t));
    if (!t) return;
    t->lines = (char (*)[T_COLS_MAX + 1])kzalloc(T_LINES * (T_COLS_MAX + 1));
    t->line_color = (unsigned char *)kzalloc(T_LINES);
    t->win = wm_create("Terminal", 8 + 60 * FONT_WIDTH + UI_SCROLL_W, 4 + 18 * FONT_HEIGHT, WF_RESIZABLE, &terminal_def);
    if (!t->lines || !t->line_color || !t->win) {
        if (t->win) wm_destroy(t->win);
        kfree(t->lines); kfree(t->line_color); kfree(t);
        return;
    }
    wm_set_min_size(t->win, 200, 96);
    t->color = C_TEXT;
    t->count = 1;
    t->cols = 60;
    t->rows = 18;
    paint(t);

    tprint(t, "Heroine kernel -- type 'guide' for commands.\n\n");
    prompt(t);

    event_t ev;
    for (;;) {
        wm_next_event(t->win, &ev, -1);
        int cw, ch;
        wm_client_size(t->win, &cw, &ch);
        switch (ev.type) {
        case EV_CLOSE:
            wm_destroy(t->win);
            kfree(t->lines); kfree(t->line_color); kfree(t);
            return;
        case EV_RESIZE:
            wm_resize_client(t->win, ev.a, ev.b);
            break;
        case EV_KEY:
            handle_key(t, ev.a);
            break;
        case EV_MENU:
            if (ev.a == T_COPY) clipboard_set(t->input);
            else if (ev.a == T_PASTE) {
                for (const char *p = clipboard_get(); *p && t->input_len < T_INPUT; p++)
                    if (*p >= 32 && *p < 127) { t->input[t->input_len++] = *p; putc_t(t, *p); }
                t->input[t->input_len] = 0;
            } else if (ev.a == T_CLEAR) { clear_screen(t); prompt(t); t->input_len = 0; }
            else if (ev.a == T_BOTTOM) t->scroll = 0;
            break;
        case EV_MOUSE_DOWN: case EV_MOUSE_MOVE: {
            int rows = t->rows, total = t->count > rows ? t->count : rows, maxs = t->count > rows ? t->count - rows : 0;
            if (ev.type == EV_MOUSE_DOWN) t->dragging_scroll = ui_inside(ev.a, ev.b, cw - UI_SCROLL_W, 0, UI_SCROLL_W, ch);
            if (t->dragging_scroll) {
                int np = ui_vscroll_click(cw - UI_SCROLL_W, 0, ch, total, rows, maxs - t->scroll, cw - UI_SCROLL_W + 4, ev.b);
                if (np >= 0) t->scroll = maxs - np;
            }
            break;
        }
        case EV_MOUSE_UP: t->dragging_scroll = 0; break;
        default: break;
        }
        paint(t);
    }
}
