#ifndef NIC_H
#define NIC_H

#include "types.h"

/* What the network stack needs from a driver. Drivers are polled -- the
 * network task calls recv() every tick, so no interrupt line is involved. */
typedef struct nic {
    const char *name;
    u8 mac[6];
    int (*send)(const void *frame, unsigned len);   /* 1 on success */
    int (*recv)(void *buf, unsigned cap);           /* frame length, 0 = nothing waiting, -1 = dropped a bad frame */
    int (*link)(void);                              /* 1 = link up */
} nic_t;

int e1000_probe(nic_t *nic);     /* Intel 8254x family (QEMU's default NIC) */
int rtl8139_probe(nic_t *nic);   /* Realtek RTL8139 (common on older real hardware) */

#endif
