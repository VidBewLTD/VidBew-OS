/* nic_rtl8139.c -- Realtek RTL8139 Ethernet driver, polled.
 *
 * Port-I/O device. Receive uses one 8KB ring buffer the card fills with
 * [status][length][frame...] records; transmit has four slots, each pointed
 * at a buffer and "started" by writing its length. */

#include "nic.h"
#include "pci.h"
#include "kstd.h"
#include "io.h"

#define REG_MAC    0x00
#define REG_TSD0   0x10
#define REG_TSAD0  0x20
#define REG_RBSTART 0x30
#define REG_CMD    0x37
#define REG_CAPR   0x38
#define REG_IMR    0x3C
#define REG_TCR    0x40
#define REG_RCR    0x44
#define REG_CONFIG1 0x52
#define REG_BMSR   0x64

#define RX_LEN 8192

static u16 io;
static u8 rxring[RX_LEN + 16 + 1536 + 4] __attribute__((aligned(16)));
static u8 txbuf[4][2048] __attribute__((aligned(16)));
static u32 rx_off = 0;
static int tx_cur = 0;

static int rtl_send(const void *frame, unsigned len)
{
    if (len == 0 || len > 1514) return 0;
    u16 tsd = (u16)(REG_TSD0 + tx_cur * 4);
    for (int i = 0; i < 100000 && !(inl((u16)(io + tsd)) & (1u << 13)); i++) io_wait();   /* OWN: DMA of the previous frame done */
    memcpy(txbuf[tx_cur], frame, len);
    unsigned send_len = len;
    if (send_len < 60) { memset(txbuf[tx_cur] + len, 0, 60 - len); send_len = 60; }
    outl((u16)(io + REG_TSAD0 + tx_cur * 4), (u32)txbuf[tx_cur]);
    outl((u16)(io + tsd), send_len);
    tx_cur = (tx_cur + 1) % 4;
    return 1;
}

static int rtl_recv(void *buf, unsigned cap)
{
    if (inb((u16)(io + REG_CMD)) & 1) return 0;          /* buffer empty */
    u8 *p = rxring + rx_off;
    u16 status = (u16)(p[0] | (p[1] << 8));
    u16 len = (u16)(p[2] | (p[3] << 8));
    int ok = (status & 1) && len >= 64 && len <= 1536 && (unsigned)(len - 4) <= cap;
    int frame_len = len - 4;                              /* trailing CRC */
    if (ok) memcpy(buf, p + 4, (unsigned)frame_len);
    rx_off = (rx_off + len + 4 + 3) & ~3u;
    rx_off %= RX_LEN;
    outw((u16)(io + REG_CAPR), (u16)(rx_off - 16));
    return ok ? frame_len : -1;
}

static int rtl_link(void) { return (inw((u16)(io + REG_BMSR)) & 4) != 0; }

int rtl8139_probe(nic_t *nic)
{
    static const u16 ids[][2] = { { 0x10EC, 0x8139 } };
    pci_dev_t dev;
    if (!pci_find(ids, 1, &dev)) return 0;
    if (!(dev.bar[0] & 1)) return 0;                      /* needs the I/O BAR */
    pci_enable(&dev, 1, 0, 1);
    io = (u16)(dev.bar[0] & ~3u);

    outb((u16)(io + REG_CONFIG1), 0);                     /* power on */
    outb((u16)(io + REG_CMD), 0x10);                      /* software reset */
    for (int i = 0; i < 100000 && (inb((u16)(io + REG_CMD)) & 0x10); i++) io_wait();

    for (int i = 0; i < 6; i++) nic->mac[i] = inb((u16)(io + REG_MAC + i));

    memset(rxring, 0, sizeof(rxring));
    rx_off = 0;
    tx_cur = 0;
    outl((u16)(io + REG_RBSTART), (u32)rxring);
    outw((u16)(io + REG_IMR), 0);                         /* polled: no interrupts */
    outl((u16)(io + REG_RCR), 0x0F | (1u << 7));          /* accept all/own/multicast/broadcast, WRAP */
    outl((u16)(io + REG_TCR), (3u << 24) | (7u << 8));
    outb((u16)(io + REG_CMD), 0x0C);                      /* enable receiver + transmitter */

    nic->name = "Realtek RTL8139";
    nic->send = rtl_send;
    nic->recv = rtl_recv;
    nic->link = rtl_link;
    return 1;
}
