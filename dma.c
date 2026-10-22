/* dma.c -- 8237 ISA DMA setup for 8-bit channels (0-3), memory -> device. */
#include "dma.h"
#include "io.h"

static const unsigned short addr_port[4]  = { 0x00, 0x02, 0x04, 0x06 };
static const unsigned short count_port[4] = { 0x01, 0x03, 0x05, 0x07 };
static const unsigned short page_port[4]  = { 0x87, 0x83, 0x81, 0x82 };

#define DMA_MASK_REG 0x0A
#define DMA_MODE_REG 0x0B
#define DMA_FLIPFLOP 0x0C

void dma_start_playback(int channel, const void *buf, unsigned int length)
{
    unsigned int addr = (unsigned int)buf;
    unsigned int count = length - 1; /* controller counts down to -1 */

    outb(DMA_MASK_REG, (unsigned char)(0x04 | channel));
    outb(DMA_FLIPFLOP, 0x00);
    outb(DMA_MODE_REG, (unsigned char)(0x48 | channel));
    outb(addr_port[channel], (unsigned char)(addr & 0xFF));
    outb(addr_port[channel], (unsigned char)((addr >> 8) & 0xFF));
    outb(page_port[channel], (unsigned char)((addr >> 16) & 0xFF));
    outb(DMA_FLIPFLOP, 0x00);
    outb(count_port[channel], (unsigned char)(count & 0xFF));
    outb(count_port[channel], (unsigned char)((count >> 8) & 0xFF));
    outb(DMA_MASK_REG, (unsigned char)channel);
}
