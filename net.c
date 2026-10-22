/* net.c -- the VidBew/OS network stack.
 *
 * Layers, bottom to top: NIC driver (nic_e1000.c / nic_rtl8139.c, polled) ->
 * Ethernet -> ARP + IPv4 -> ICMP (ping) and UDP -> DHCP client and DNS
 * resolver. One background task, "netd", polls the NIC every tick and feeds
 * frames up the stack. Anything that has to *wait* for an answer (a ping, a
 * DNS lookup, DHCP) runs in the caller's task or a short-lived helper, never
 * in netd, so receiving never stalls.
 *
 * Not here yet: TCP, IP fragmentation, IPv6. */

#include "net.h"
#include "nic.h"
#include "task.h"
#include "timer.h"
#include "sync.h"
#include "kstd.h"
#include "serial.h"

static nic_t nic;
static int have_nic = 0;
static net_config_t cfg;
static mutex_t tx_lock, ping_lock, dns_lock, rx_lock;
static unsigned rx_count, tx_count, err_count;
static u32 tsc_khz = 0;                 /* TSC cycles per millisecond, calibrated by netd */

static const u8 BCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 ZERO_IP[4] = { 0, 0, 0, 0 };
static const u8 BCAST_IP[4] = { 255, 255, 255, 255 };

static inline u16 rd16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline void wr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static inline u32 rd32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static inline u32 tsc_lo(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return lo; }

static u16 checksum(const u8 *p, int len)
{
    u32 sum = 0;
    for (int i = 0; i + 1 < len; i += 2) sum += (u32)((p[i] << 8) | p[i + 1]);
    if (len & 1) sum += (u32)(p[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

/* ---------------------------------------------------------------- helpers */

int net_parse_ip(const char *s, u8 out[4])
{
    int part = 0, val = 0, digits = 0;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (++digits > 3 || val > 255) return 0;
        } else if (*s == '.' || *s == 0) {
            if (!digits) return 0;
            out[part++] = (u8)val;
            val = 0; digits = 0;
            if (*s == 0) break;
            if (part == 4) return 0;
        } else return 0;
    }
    return part == 4;
}

void net_fmt_ip(const u8 ip[4], char *out) { ksnprintf(out, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]); }
void net_fmt_mac(const u8 m[6], char *out) { ksnprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]); }

static int ip_eq(const u8 *a, const u8 *b) { return memcmp(a, b, 4) == 0; }

static int on_link(const u8 dst[4])
{
    for (int i = 0; i < 4; i++)
        if ((dst[i] & cfg.mask[i]) != (cfg.ip[i] & cfg.mask[i])) return 0;
    return 1;
}

/* ---------------------------------------------------------------- Ethernet + ARP */

static u8 txframe[1600];

static void eth_send(const u8 dst_mac[6], u16 type, const void *payload, int len)
{
    if (!have_nic || len < 0 || len > 1500) return;
    mutex_lock(&tx_lock);
    memcpy(txframe, dst_mac, 6);
    memcpy(txframe + 6, cfg.mac, 6);
    wr16(txframe + 12, type);
    memcpy(txframe + 14, payload, (unsigned)len);
    int total = 14 + len;
    if (total < 60) { memset(txframe + total, 0, (unsigned)(60 - total)); total = 60; }
    if (nic.send(txframe, (unsigned)total)) tx_count++; else err_count++;
    mutex_unlock(&tx_lock);
}

typedef struct { u8 ip[4], mac[6]; int valid; } arp_ent_t;
static arp_ent_t arp_cache[8];
static int arp_next = 0;

static void arp_learn(const u8 ip[4], const u8 mac[6])
{
    u32 f = irq_save();
    for (int i = 0; i < 8; i++)
        if (arp_cache[i].valid && ip_eq(arp_cache[i].ip, ip)) { memcpy(arp_cache[i].mac, mac, 6); irq_restore(f); return; }
    int slot = -1;
    for (int i = 0; i < 8; i++) if (!arp_cache[i].valid) { slot = i; break; }
    if (slot < 0) { slot = arp_next; arp_next = (arp_next + 1) % 8; }
    memcpy(arp_cache[slot].ip, ip, 4);
    memcpy(arp_cache[slot].mac, mac, 6);
    arp_cache[slot].valid = 1;
    irq_restore(f);
}

static int arp_lookup(const u8 ip[4], u8 mac[6])
{
    int found = 0;
    u32 f = irq_save();
    for (int i = 0; i < 8; i++)
        if (arp_cache[i].valid && ip_eq(arp_cache[i].ip, ip)) { memcpy(mac, arp_cache[i].mac, 6); found = 1; break; }
    irq_restore(f);
    return found;
}

static void arp_send(int oper, const u8 tha[6], const u8 tpa[4])
{
    u8 p[28];
    wr16(p, 1); wr16(p + 2, 0x0800); p[4] = 6; p[5] = 4; wr16(p + 6, (u16)oper);
    memcpy(p + 8, cfg.mac, 6);
    memcpy(p + 14, cfg.ip, 4);
    memcpy(p + 18, oper == 1 ? (const u8 *)"\0\0\0\0\0\0" : tha, 6);
    memcpy(p + 24, tpa, 4);
    eth_send(oper == 1 ? BCAST_MAC : tha, 0x0806, p, 28);
}

static void arp_rx(const u8 *p, int len)
{
    if (len < 28 || rd16(p) != 1 || rd16(p + 2) != 0x0800) return;
    const u8 *sha = p + 8, *spa = p + 14, *tpa = p + 24;
    if (!ip_eq(spa, ZERO_IP)) arp_learn(spa, sha);
    if (rd16(p + 6) == 1 && cfg.configured && ip_eq(tpa, cfg.ip)) arp_send(2, sha, spa);
}

static int arp_resolve(const u8 ip[4], u8 mac[6])
{
    for (int attempt = 0; attempt < 4; attempt++) {
        if (arp_lookup(ip, mac)) return 1;
        arp_send(1, BCAST_MAC, ip);
        for (int t = 0; t < 25; t++) {
            task_sleep(1);
            if (arp_lookup(ip, mac)) return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- IPv4 */

static u8 ipbuf[1520];
static u16 ip_ident = 1;

/* find the MAC to hand a packet for `dst` to: broadcast, on-link host, or the gateway */
static int next_hop_mac(const u8 dst[4], u8 mac[6])
{
    if (ip_eq(dst, BCAST_IP)) { memcpy(mac, BCAST_MAC, 6); return 1; }
    if (!cfg.configured) return 0;
    const u8 *hop = on_link(dst) ? dst : cfg.gw;
    return arp_resolve(hop, mac);
}

static int ip_send_mac(const u8 src[4], const u8 dst[4], const u8 dmac[6], u8 proto, const u8 *payload, int len)
{
    if (len < 0 || len > 1480) return 0;
    mutex_lock(&tx_lock);
    u8 *h = ipbuf;
    h[0] = 0x45; h[1] = 0;
    wr16(h + 2, (u16)(20 + len));
    wr16(h + 4, ip_ident++);
    wr16(h + 6, 0);
    h[8] = 64; h[9] = proto;
    wr16(h + 10, 0);
    memcpy(h + 12, src, 4);
    memcpy(h + 16, dst, 4);
    wr16(h + 10, checksum(h, 20));
    memcpy(h + 20, payload, (unsigned)len);
    eth_send(dmac, 0x0800, h, 20 + len);
    mutex_unlock(&tx_lock);
    return 1;
}

static void poll_rx(void);

/* ---------------------------------------------------------------- ICMP (ping) */

#define PING_ID 0xBE57
static volatile int ping_wait_seq = -1, ping_got = 0;
static volatile u32 ping_reply_tsc = 0;
static volatile unsigned long ping_reply_tick = 0;
static u16 ping_seq = 0;

static void icmp_rx(const u8 *src_ip, const u8 *src_mac, const u8 *p, int len)
{
    if (len < 8) return;
    if (p[0] == 8 && cfg.configured) {                 /* echo request: answer it */
        static u8 reply[1500];
        if (len > 1480) return;
        memcpy(reply, p, (unsigned)len);
        reply[0] = 0;
        wr16(reply + 2, 0);
        wr16(reply + 2, checksum(reply, len));
        ip_send_mac(cfg.ip, src_ip, src_mac, 1, reply, len);
    } else if (p[0] == 0) {                            /* echo reply */
        if (rd16(p + 4) == PING_ID && (int)rd16(p + 6) == ping_wait_seq) {
            ping_reply_tsc = tsc_lo();
            ping_reply_tick = timer_ticks;
            ping_got = 1;
        }
    }
}

int net_ping(const u8 ip[4], int timeout_ms, int *rtt_ms)
{
    if (!have_nic || !cfg.configured) return 0;
    u8 dmac[6];
    if (!next_hop_mac(ip, dmac)) return 0;
    mutex_lock(&ping_lock);
    u8 pkt[40];
    ping_seq++;
    pkt[0] = 8; pkt[1] = 0;
    wr16(pkt + 2, 0);
    wr16(pkt + 4, PING_ID);
    wr16(pkt + 6, ping_seq);
    for (int i = 8; i < 40; i++) pkt[i] = (u8)('a' + (i % 23));
    wr16(pkt + 2, checksum(pkt, 40));
    ping_got = 0;
    ping_wait_seq = ping_seq;
    unsigned long t0 = timer_ticks;
    u32 c0 = tsc_lo();
    ip_send_mac(cfg.ip, ip, dmac, 1, pkt, 40);
    int ok = 0;
    u32 spin_end = tsc_khz ? c0 + tsc_khz * 30 : 0;   /* busy-poll ~30ms: LAN replies land in well under a tick */
    while (spin_end && (int)(tsc_lo() - spin_end) < 0) {
        poll_rx();
        if (ping_got) { ok = 1; break; }
    }
    while (!ok && (int)(timer_ticks - t0) * 10 < timeout_ms) {
        if (ping_got) { ok = 1; break; }
        task_sleep(1);
    }
    if (ok && rtt_ms) {
        if (tsc_khz) *rtt_ms = (int)((ping_reply_tsc - c0) / tsc_khz);
        else *rtt_ms = (int)(ping_reply_tick - t0) * 10;
    }
    ping_wait_seq = -1;
    mutex_unlock(&ping_lock);
    return ok;
}

/* ---------------------------------------------------------------- UDP */

static u8 udpbuf[1500];

static int udp_send(const u8 src_ip[4], const u8 dst_ip[4], const u8 dmac[6], u16 sport, u16 dport, const u8 *data, int len)
{
    if (len < 0 || len > 1472) return 0;
    mutex_lock(&tx_lock);
    wr16(udpbuf, sport);
    wr16(udpbuf + 2, dport);
    wr16(udpbuf + 4, (u16)(8 + len));
    wr16(udpbuf + 6, 0);                                 /* checksum optional over IPv4 */
    memcpy(udpbuf + 8, data, (unsigned)len);
    int r = ip_send_mac(src_ip, dst_ip, dmac, 17, udpbuf, 8 + len);
    mutex_unlock(&tx_lock);
    return r;
}

/* ---------------------------------------------------------------- DHCP */

static volatile int dhcp_busy_flag = 0, dhcp_wanted = 0;
static u32 dhcp_xid = 0x56420000;
static volatile int dhcp_rx_type = 0;
static u8 dh_offer[4], dh_srv[4], dh_mask[4], dh_gw[4], dh_dns[4];

static void dhcp_send(int type, const u8 *req_ip, const u8 *srv)
{
    u8 m[300];
    memset(m, 0, sizeof(m));
    m[0] = 1; m[1] = 1; m[2] = 6;
    m[4] = (u8)(dhcp_xid >> 24); m[5] = (u8)(dhcp_xid >> 16); m[6] = (u8)(dhcp_xid >> 8); m[7] = (u8)dhcp_xid;
    wr16(m + 10, 0x8000);                                /* ask for a broadcast reply */
    memcpy(m + 28, cfg.mac, 6);
    m[236] = 0x63; m[237] = 0x82; m[238] = 0x53; m[239] = 0x63;
    int o = 240;
    m[o++] = 53; m[o++] = 1; m[o++] = (u8)type;
    m[o++] = 61; m[o++] = 7; m[o++] = 1; memcpy(m + o, cfg.mac, 6); o += 6;
    if (req_ip) { m[o++] = 50; m[o++] = 4; memcpy(m + o, req_ip, 4); o += 4; }
    if (srv) { m[o++] = 54; m[o++] = 4; memcpy(m + o, srv, 4); o += 4; }
    m[o++] = 55; m[o++] = 3; m[o++] = 1; m[o++] = 3; m[o++] = 6;
    m[o++] = 255;
    udp_send(ZERO_IP, BCAST_IP, BCAST_MAC, 68, 67, m, 300);
}

static void dhcp_rx(const u8 *p, int len)
{
    if (len < 241 || p[0] != 2 || rd32(p + 4) != dhcp_xid) return;
    if (memcmp(p + 28, cfg.mac, 6) != 0) return;
    if (p[236] != 0x63 || p[237] != 0x82 || p[238] != 0x53 || p[239] != 0x63) return;
    int type = 0;
    u8 mask[4] = { 255, 255, 255, 0 }, gw[4] = { 0, 0, 0, 0 }, dns[4] = { 0, 0, 0, 0 }, srv[4] = { 0, 0, 0, 0 };
    for (int o = 240; o + 1 < len;) {
        int code = p[o++];
        if (code == 255) break;
        if (code == 0) continue;
        int l = p[o++];
        if (o + l > len) break;
        switch (code) {
        case 53: type = p[o]; break;
        case 1: if (l >= 4) memcpy(mask, p + o, 4); break;
        case 3: if (l >= 4) memcpy(gw, p + o, 4); break;
        case 6: if (l >= 4) memcpy(dns, p + o, 4); break;
        case 54: if (l >= 4) memcpy(srv, p + o, 4); break;
        default: break;
        }
        o += l;
    }
    memcpy(dh_offer, p + 16, 4);
    memcpy(dh_mask, mask, 4); memcpy(dh_gw, gw, 4); memcpy(dh_dns, dns, 4); memcpy(dh_srv, srv, 4);
    dhcp_rx_type = type;
}

static int dhcp_wait(int want, int ms)
{
    unsigned long t0 = timer_ticks;
    while ((int)(timer_ticks - t0) * 10 < ms) {
        if (dhcp_rx_type == want) return 1;
        if (dhcp_rx_type == 6) return 0;                 /* NAK */
        task_sleep(1);
    }
    return 0;
}

static void dhcp_task(void *arg)
{
    serial_print("net: DHCP starting\n");
    cfg.configured = 0;
    cfg.dhcp = 1;
    memset(cfg.ip, 0, 4);
    int got = 0;
    for (int attempt = 0; attempt < 4 && !got; attempt++) {
        dhcp_xid += 0x101;
        dhcp_rx_type = 0;
        dhcp_send(1, 0, 0);
        if (!dhcp_wait(2, 2500)) continue;
        u8 offered[4], server[4];
        memcpy(offered, dh_offer, 4);
        memcpy(server, dh_srv, 4);
        for (int r = 0; r < 3 && !got; r++) {
            dhcp_rx_type = 0;
            dhcp_send(3, offered, server);
            if (dhcp_wait(5, 2500)) got = 1;
        }
    }
    if (got) {
        u32 f = irq_save();
        memcpy(cfg.ip, dh_offer, 4);
        memcpy(cfg.mask, dh_mask, 4);
        memcpy(cfg.gw, dh_gw, 4);
        if (ip_eq(dh_dns, ZERO_IP)) memcpy(cfg.dns, dh_gw, 4); else memcpy(cfg.dns, dh_dns, 4);
        cfg.configured = 1;
        cfg.dhcp = 1;
        irq_restore(f);
        char a[16], g[16];
        net_fmt_ip(cfg.ip, a);
        net_fmt_ip(cfg.gw, g);
        char m[80];
        ksnprintf(m, sizeof(m), "net: DHCP lease %s gateway %s\n", a, g);
        serial_print(m);
    } else {
        serial_print("net: DHCP got no answer\n");
    }
    dhcp_busy_flag = 0;
}

void net_request_dhcp(void) { dhcp_wanted = 1; }
int net_dhcp_busy(void) { return dhcp_busy_flag || dhcp_wanted; }

/* ---------------------------------------------------------------- DNS */

static volatile int dns_waiting = 0;
static volatile u16 dns_wait_port = 0, dns_wait_id = 0;
static volatile int dns_done = 0;
static u8 dns_result[4];
static u16 dns_counter = 0;

static void dns_rx(const u8 *p, int len)
{
    if (!dns_waiting || len < 12 || rd16(p) != dns_wait_id) return;
    int flags = rd16(p + 2);
    if (!(flags & 0x8000) || (flags & 0x000F) != 0) { dns_done = -1; return; }
    int qd = rd16(p + 4), an = rd16(p + 6);
    int o = 12;
    for (int q = 0; q < qd; q++) {                       /* skip the echoed question */
        while (o < len && p[o]) { if ((p[o] & 0xC0) == 0xC0) { o++; break; } o += p[o] + 1; }
        o += 1 + 4;
    }
    for (int a = 0; a < an && o < len; a++) {
        if ((p[o] & 0xC0) == 0xC0) o += 2;
        else { while (o < len && p[o]) o += p[o] + 1; o++; }
        if (o + 10 > len) break;
        int type = rd16(p + o), rdlen = rd16(p + o + 8);
        o += 10;
        if (type == 1 && rdlen == 4 && o + 4 <= len) {
            memcpy(dns_result, p + o, 4);
            dns_done = 1;
            return;
        }
        o += rdlen;
    }
    dns_done = -1;
}

int net_resolve(const char *name, u8 ip_out[4], int timeout_ms)
{
    if (!have_nic || !cfg.configured || !name[0]) return 0;
    u8 dmac[6];
    if (!next_hop_mac(cfg.dns, dmac)) return 0;
    mutex_lock(&dns_lock);

    u8 q[300];
    int o = 12;
    dns_wait_id = (u16)(0xB000 + ++dns_counter);
    dns_wait_port = (u16)(49152 + (dns_counter & 0x3FFF));
    memset(q, 0, 12);
    wr16(q, dns_wait_id);
    wr16(q + 2, 0x0100);
    wr16(q + 4, 1);
    const char *s = name;
    while (*s) {                                         /* name -> length-prefixed labels */
        const char *dot = s;
        while (*dot && *dot != '.') dot++;
        int l = (int)(dot - s);
        if (l == 0 || l > 63 || o + l + 6 > (int)sizeof(q)) { mutex_unlock(&dns_lock); return 0; }
        q[o++] = (u8)l;
        memcpy(q + o, s, (unsigned)l);
        o += l;
        s = *dot ? dot + 1 : dot;
    }
    q[o++] = 0;
    wr16(q + o, 1); wr16(q + o + 2, 1);                  /* type A, class IN */
    o += 4;

    dns_done = 0;
    dns_waiting = 1;
    int ok = 0;
    for (int attempt = 0; attempt < 2 && !ok && dns_done == 0; attempt++) {
        udp_send(cfg.ip, cfg.dns, dmac, dns_wait_port, 53, q, o);
        unsigned long t0 = timer_ticks;
        while ((int)(timer_ticks - t0) * 10 < timeout_ms / 2) {
            if (dns_done) break;
            task_sleep(1);
        }
    }
    if (dns_done == 1) { memcpy(ip_out, dns_result, 4); ok = 1; }
    dns_waiting = 0;
    mutex_unlock(&dns_lock);
    return ok;
}

/* ---------------------------------------------------------------- receive path */

static void udp_rx(const u8 *src_ip, const u8 *p, int len)
{
    if (len < 8) return;
    u16 sport = rd16(p), dport = rd16(p + 2);
    int ulen = rd16(p + 4);
    if (ulen < 8 || ulen > len) return;
    const u8 *data = p + 8;
    int dlen = ulen - 8;
    if (dport == 68) dhcp_rx(data, dlen);
    else if (sport == 53 && dport == dns_wait_port) dns_rx(data, dlen);
    (void)src_ip;
}

static void ip_rx(const u8 *src_mac, const u8 *p, int len)
{
    if (len < 20 || (p[0] >> 4) != 4) return;
    int ihl = (p[0] & 0x0F) * 4;
    int total = rd16(p + 2);
    if (ihl < 20 || total < ihl || total > len) return;
    if (rd16(p + 6) & 0x3FFF) return;                    /* fragments: not supported */
    const u8 *src = p + 12, *dst = p + 16;
    int ours = ip_eq(dst, cfg.ip) || ip_eq(dst, BCAST_IP) || !cfg.configured;
    if (!ours) return;
    if (!ip_eq(src, ZERO_IP)) arp_learn(src, src_mac);
    const u8 *payload = p + ihl;
    int plen = total - ihl;
    if (p[9] == 1) icmp_rx(src, src_mac, payload, plen);
    else if (p[9] == 17) udp_rx(src, payload, plen);
}

static void handle_frame(const u8 *f, int len)
{
    if (len < 14) return;
    if (memcmp(f, cfg.mac, 6) != 0 && memcmp(f, BCAST_MAC, 6) != 0) return;
    u16 type = rd16(f + 12);
    if (type == 0x0806) arp_rx(f + 14, len - 14);
    else if (type == 0x0800) ip_rx(f + 6, f + 14, len - 14);
}

/* Drain the NIC. Called by netd every tick, and by a task that is waiting
 * for a ping reply so it can see the answer within microseconds instead of
 * at the next 10ms tick. */
static void poll_rx(void)
{
    static u8 rxbuf[2048];
    mutex_lock(&rx_lock);
    for (int i = 0; i < 64; i++) {
        int n = nic.recv(rxbuf, sizeof(rxbuf));
        if (n == 0) break;
        if (n < 0) { err_count++; continue; }
        rx_count++;
        handle_frame(rxbuf, n);
    }
    mutex_unlock(&rx_lock);
}

static void netd_task(void *arg)
{
    /* measure the TSC against the 100 Hz timer so pings can report sub-tick times */
    unsigned long k0 = timer_ticks;
    u32 c0 = tsc_lo();
    while (timer_ticks - k0 < 10) task_sleep(1);
    unsigned long dk = timer_ticks - k0;
    u32 dc = tsc_lo() - c0;
    if (dk) tsc_khz = dc / (u32)(dk * 10);
    if (tsc_khz < 1000) tsc_khz = 0;                     /* implausible: fall back to tick timing */

    if (!cfg.configured) dhcp_wanted = 1;

    for (;;) {
        poll_rx();
        if (dhcp_wanted && !dhcp_busy_flag) {
            dhcp_wanted = 0;
            dhcp_busy_flag = 1;
            if (task_create("dhcp", dhcp_task, 0, 0) < 0) dhcp_busy_flag = 0;
        }
        cfg.link = nic.link();
        task_sleep(1);
    }
}

/* ---------------------------------------------------------------- public API */

int net_init(void)
{
    mutex_init(&tx_lock);
    mutex_init(&ping_lock);
    mutex_init(&dns_lock);
    mutex_init(&rx_lock);
    memset(&cfg, 0, sizeof(cfg));
    memset(arp_cache, 0, sizeof(arp_cache));
    if (e1000_probe(&nic) || rtl8139_probe(&nic)) {
        have_nic = 1;
        memcpy(cfg.mac, nic.mac, 6);
        cfg.link = nic.link();
        memcpy(cfg.mask, "\xFF\xFF\xFF\x00", 4);
        char m[80], mac[20];
        net_fmt_mac(nic.mac, mac);
        ksnprintf(m, sizeof(m), "net: %s, MAC %s\n", nic.name, mac);
        serial_print(m);
        if (task_create("netd", netd_task, 0, 0) < 0) have_nic = 0;
    }
    return have_nic;
}

int net_present(void) { return have_nic; }
const char *net_nic_name(void) { return have_nic ? nic.name : "none"; }
void net_get_config(net_config_t *c) { *c = cfg; }

int net_tray_state(void)
{
    if (!have_nic || !cfg.link) return 0;
    return cfg.configured ? 2 : 1;
}

void net_set_static(const u8 ip[4], const u8 mask[4], const u8 gw[4], const u8 dns[4])
{
    u32 f = irq_save();
    memcpy(cfg.ip, ip, 4); memcpy(cfg.mask, mask, 4); memcpy(cfg.gw, gw, 4); memcpy(cfg.dns, dns, 4);
    cfg.configured = 1;
    cfg.dhcp = 0;
    memset(arp_cache, 0, sizeof(arp_cache));
    irq_restore(f);
}

void net_set_dns(const u8 dns[4]) { memcpy(cfg.dns, dns, 4); }
void net_stats(unsigned *rx, unsigned *tx, unsigned *errors) { *rx = rx_count; *tx = tx_count; *errors = err_count; }
