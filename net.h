#ifndef NET_H
#define NET_H

#include "types.h"

/* VidBew/OS network stack: PCI NIC drivers (Intel e1000 and Realtek RTL8139),
 * Ethernet, ARP, IPv4, ICMP, UDP, a DHCP client and a DNS resolver. All
 * blocking calls may be made from any task; a background "netd" task does the
 * receiving. There is no TCP yet. */

typedef struct {
    u8 ip[4], mask[4], gw[4], dns[4];
    u8 mac[6];
    int link;          /* cable/link detected */
    int configured;    /* has an IP address */
    int dhcp;          /* address came from (or is being requested via) DHCP */
} net_config_t;

int  net_init(void);                       /* find a NIC and start netd; 1 if one was found */
int  net_present(void);
const char *net_nic_name(void);
void net_get_config(net_config_t *c);
int  net_tray_state(void);                 /* 0 = no NIC/link, 1 = link but no address, 2 = ready */
void net_request_dhcp(void);               /* non-blocking: netd (re)runs DHCP */
int  net_dhcp_busy(void);
void net_set_static(const u8 ip[4], const u8 mask[4], const u8 gw[4], const u8 dns[4]);
void net_set_dns(const u8 dns[4]);
int  net_ping(const u8 ip[4], int timeout_ms, int *rtt_ms);   /* blocking; 1 = reply */
int  net_resolve(const char *name, u8 ip_out[4], int timeout_ms);
void net_stats(unsigned *rx, unsigned *tx, unsigned *errors);
int  net_parse_ip(const char *s, u8 out[4]);   /* dotted quad; 1 if valid */
void net_fmt_ip(const u8 ip[4], char *out);    /* out needs 16 bytes */
void net_fmt_mac(const u8 mac[6], char *out);  /* out needs 18 bytes */

#endif
