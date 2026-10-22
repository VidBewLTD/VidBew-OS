/* nic_e1000.c -- Intel 8254x ("e1000") Ethernet driver, polled.
 *
 * This is QEMU's default NIC. The device is memory-mapped; packets move
 * through two rings of 16-byte descriptors in RAM. For receive we hand the
 * card a ring of empty 2KB buffers and watch each descriptor's "descriptor
 * done" bit; for transmit we fill a descriptor and bump the tail register. */

#include "nic.h"
#include "pci.h"
#include "kstd.h"
#include "io.h"
#include "serial.h"

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL0   0x5400
#define REG_RAH0   0x5404

#define RX_N 16
#define TX_N 16
#define BUF  2048

typedef struct { u32 addr_lo, addr_hi; u16 length, csum; u8 status, errors; u16 special; } __attribute__((packed)) rx_desc_t;
typedef struct { u32 addr_lo, addr_hi; u16 length; u8 cso, cmd, status, css; u16 special; } __attribute__((packed)) tx_desc_t;

static volatile u32 *mmio;
static rx_desc_t rxd[RX_N] __attribute__((aligned(128)));
static tx_desc_t txd[TX_N] __attribute__((aligned(128)));
static u8 rxbuf[RX_N][BUF] __attribute__((aligned(16)));
static u8 txbuf[TX_N][BUF] __attribute__((aligned(16)));
static int rx_cur = 0;

static u32 rd(u32 r) { return mmio[r / 4]; }
static void wr(u32 r, u32 v) { mmio[r / 4] = v; }

static void spin_ms(int ms)
{
    for (int i = 0; i < ms * 1000; i++) io_wait();
}

static u16 eeprom_read(int word)
{
    wr(REG_EERD, 1u | ((u32)word << 8));
    for (int i = 0; i < 10000; i++) {
        u32 v = rd(REG_EERD);
        if (v & 0x10) return (u16)(v >> 16);
    }
    return 0;
}

static int e1000_send(const void *frame, unsigned len)
{
    if (len == 0 || len > 1514) return 0;
    u32 tail = rd(REG_TDT);
    tx_desc_t *d = &txd[tail];
    for (int i = 0; i < 100000 && d->cmd && !(d->status & 1); i++) io_wait();   /* previous use still in flight */
    memcpy(txbuf[tail], frame, len);
    d->addr_lo = (u32)txbuf[tail];
    d->addr_hi = 0;
    d->length = (u16)len;
    d->cso = 0;
    d->cmd = 0x0B;          /* end of packet | insert FCS | report status */
    d->status = 0;
    d->css = 0;
    d->special = 0;
    wr(REG_TDT, (tail + 1) % TX_N);
    return 1;
}

static int e1000_recv(void *buf, unsigned cap)
{
    rx_desc_t *d = &rxd[rx_cur];
    if (!(d->status & 1)) return 0;
    int len = d->length;
    int ok = (d->errors == 0) && (d->status & 2) && len > 0 && (unsigned)len <= cap;
    if (ok) memcpy(buf, rxbuf[rx_cur], (unsigned)len);
    d->status = 0;
    wr(REG_RDT, (u32)rx_cur);
    rx_cur = (rx_cur + 1) % RX_N;
    return ok ? len : -1;
}

static int e1000_link(void) { return (rd(REG_STATUS) & 2) != 0; }

int e1000_probe(nic_t *nic)
{
    static const u16 ids[][2] = {
        { 0x8086, 0x100E }, { 0x8086, 0x100F }, { 0x8086, 0x1004 }, { 0x8086, 0x1008 },
        { 0x8086, 0x1009 }, { 0x8086, 0x100C }, { 0x8086, 0x1010 }, { 0x8086, 0x1013 },
        { 0x8086, 0x1015 }, { 0x8086, 0x1016 }, { 0x8086, 0x1017 }, { 0x8086, 0x1018 },
        { 0x8086, 0x1019 }, { 0x8086, 0x1075 }, { 0x8086, 0x1076 }, { 0x8086, 0x1077 },
        { 0x8086, 0x1078 }, { 0x8086, 0x1079 }, { 0x8086, 0x107A }, { 0x8086, 0x107B },
    };
    pci_dev_t dev;
    if (!pci_find(ids, (int)(sizeof(ids) / sizeof(ids[0])), &dev)) return 0;
    if (dev.bar[0] & 1) return 0;              /* we need the memory-mapped BAR */
    pci_enable(&dev, 0, 1, 1);
    mmio = (volatile u32 *)(dev.bar[0] & ~0xFu);

    wr(REG_IMC, 0xFFFFFFFFu);
    wr(REG_CTRL, rd(REG_CTRL) | (1u << 26));   /* reset */
    spin_ms(20);
    wr(REG_IMC, 0xFFFFFFFFu);
    wr(REG_CTRL, (rd(REG_CTRL) | (1u << 6) | (1u << 5)) & ~((1u << 3) | (1u << 31)));   /* set link up */

    u32 ral = rd(REG_RAL0), rah = rd(REG_RAH0);
    if (rah & 0x80000000u) {
        nic->mac[0] = (u8)ral; nic->mac[1] = (u8)(ral >> 8); nic->mac[2] = (u8)(ral >> 16); nic->mac[3] = (u8)(ral >> 24);
        nic->mac[4] = (u8)rah; nic->mac[5] = (u8)(rah >> 8);
    } else {
        for (int i = 0; i < 3; i++) {
            u16 w = eeprom_read(i);
            nic->mac[i * 2] = (u8)w;
            nic->mac[i * 2 + 1] = (u8)(w >> 8);
        }
    }
    wr(REG_RAL0, (u32)nic->mac[0] | ((u32)nic->mac[1] << 8) | ((u32)nic->mac[2] << 16) | ((u32)nic->mac[3] << 24));
    wr(REG_RAH0, (u32)nic->mac[4] | ((u32)nic->mac[5] << 8) | 0x80000000u);
    for (int i = 0; i < 128; i++) wr(REG_MTA + (u32)i * 4, 0);

    for (int i = 0; i < RX_N; i++) {
        memset(&rxd[i], 0, sizeof(rxd[i]));
        rxd[i].addr_lo = (u32)rxbuf[i];
    }
    wr(REG_RDBAL, (u32)rxd);
    wr(REG_RDBAH, 0);
    wr(REG_RDLEN, RX_N * sizeof(rx_desc_t));
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_N - 1);
    rx_cur = 0;
    wr(REG_RCTL, (1u << 1) | (1u << 15) | (1u << 26));   /* enable, accept broadcast, strip CRC, 2KB buffers */

    memset(txd, 0, sizeof(txd));
    wr(REG_TDBAL, (u32)txd);
    wr(REG_TDBAH, 0);
    wr(REG_TDLEN, TX_N * sizeof(tx_desc_t));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TCTL, (1u << 1) | (1u << 3) | (0x10u << 4) | (0x40u << 12));
    wr(REG_TIPG, 0x0060200A);

    nic->name = "Intel PRO/1000 (e1000)";
    nic->send = e1000_send;
    nic->recv = e1000_recv;
    nic->link = e1000_link;
    return 1;
}
