/* sb16.c -- Sound Blaster 16 DSP + 8-bit DMA playback.
 * The buffer is a 64KB-aligned static array: 8-bit ISA DMA can't cross
 * a 64KB physical boundary mid-transfer. */
#include "sb16.h"
#include "dma.h"
#include "io.h"
#include "timer.h"
#include "task.h"
#include "heap.h"

#define SB_BASE        0x220
#define SB_RESET       (SB_BASE + 0x6)
#define SB_READ_DATA   (SB_BASE + 0xA)
#define SB_WRITE_CMD   (SB_BASE + 0xC)
#define SB_READ_STATUS (SB_BASE + 0xE)
#define SB_DMA_CHANNEL 1

static unsigned char *playback_buffer = 0;   /* 64KB-aligned, carved out of the heap at init */

static int present = 0;
static int volume_pct = 80;

static void tiny_delay(void) { for (int i = 0; i < 8; i++) inb(0x80); }

static void dsp_write(unsigned char value)
{
    int timeout = 100000;
    while (timeout-- && (inb(SB_WRITE_CMD) & 0x80));
    outb(SB_WRITE_CMD, value);
}

int sb16_init(void)
{
    outb(SB_RESET, 1);
    tiny_delay();
    outb(SB_RESET, 0);

    int timeout = 100000;
    while (timeout-- && !(inb(SB_READ_STATUS) & 0x80));
    if (timeout <= 0) return 0;
    present = (inb(SB_READ_DATA) == 0xAA);
    if (present) {
        /* ISA DMA needs one physically contiguous, 64KB-aligned block below 16MB */
        unsigned char *raw = (unsigned char *)kmalloc(2 * SB16_BUFFER_CAPACITY);
        if (raw) playback_buffer = (unsigned char *)(((unsigned int)raw + 65535u) & ~65535u);
        if (!playback_buffer || (unsigned int)playback_buffer + SB16_BUFFER_CAPACITY > 0x1000000u) {
            playback_buffer = 0;
            present = 0;
        }
    }
    if (present) sb16_set_volume(volume_pct);
    return present;
}

int sb16_present(void) { return present; }
int sb16_get_volume(void) { return volume_pct; }

/* SB16 mixer: 0x224 = index, 0x225 = data. Registers 0x30/0x31 are master
 * left/right and 0x32/0x33 voice left/right, each a 5-bit level in the top
 * bits of the byte. */
void sb16_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    volume_pct = pct;
    if (!present) return;
    unsigned char level = (unsigned char)(((pct * 31) / 100) << 3);
    for (unsigned char reg = 0x30; reg <= 0x33; reg++) {
        outb(SB_BASE + 4, reg);
        outb(SB_BASE + 5, level);
    }
}

void sb16_play_u8_mono(const unsigned char *data, unsigned int length, unsigned int sample_rate)
{
    if (length > SB16_BUFFER_CAPACITY) length = SB16_BUFFER_CAPACITY;
    if (length == 0 || sample_rate == 0 || !playback_buffer) return;

    for (unsigned int i = 0; i < length; i++) playback_buffer[i] = data[i];

    dma_start_playback(SB_DMA_CHANNEL, playback_buffer, length);

    dsp_write(0x41);
    dsp_write((unsigned char)((sample_rate >> 8) & 0xFF));
    dsp_write((unsigned char)(sample_rate & 0xFF));

    unsigned int count = length - 1;
    dsp_write(0x14);
    dsp_write((unsigned char)(count & 0xFF));
    dsp_write((unsigned char)((count >> 8) & 0xFF));

    /* No playback-complete IRQ handler yet: we know the exact duration. */
    unsigned long wait_ticks = ((unsigned long)length * 100) / sample_rate + 5;
    unsigned long start = timer_ticks;
    while (timer_ticks - start < wait_ticks) task_sleep(1);

    (void)inb(SB_READ_STATUS);
}
