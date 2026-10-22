/* ata.c -- ATA PIO mode disk driver (primary bus, master drive, LBA28).
 *
 * This is the driver that lets the *kernel* (running in 32-bit
 * protected mode) read and write disk sectors. It's a separate thing
 * from the bootloader's disk reads -- those used the BIOS (INT 0x13),
 * which is a real-mode-only service and isn't callable anymore once
 * we're in protected mode. From here on, all disk I/O goes through
 * direct port access to the IDE/ATA controller instead.
 */

#include "ata.h"
#include "io.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static int ata_wait_ready(void)
{
    int timeout = 1000000;
    unsigned char status;
    do {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) return 0;
    } while ((status & ATA_SR_BSY) && --timeout);
    return timeout != 0;
}

static int ata_wait_drq(void)
{
    int timeout = 1000000;
    unsigned char status;
    do {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) return 0;
        if (status & ATA_SR_DRQ) return 1;
    } while (--timeout);
    return 0;
}

static void ata_select_and_setup(unsigned int lba, unsigned char count)
{
    outb(ATA_DRIVE_HEAD, (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));
    io_wait();
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LOW,  (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID,  (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
}

int ata_read_sectors(unsigned int lba, unsigned char count, void *buf)
{
    unsigned short *dst = (unsigned short *)buf;

    if (!ata_wait_ready()) return 0;
    ata_select_and_setup(lba, count);
    outb(ATA_COMMAND, ATA_CMD_READ);

    int total = count ? count : 256;
    for (int s = 0; s < total; s++) {
        if (!ata_wait_drq()) return 0;
        for (int i = 0; i < 256; i++) {
            dst[s * 256 + i] = inw(ATA_DATA);
        }
    }
    return 1;
}

int ata_write_sectors(unsigned int lba, unsigned char count, const void *buf)
{
    const unsigned short *src = (const unsigned short *)buf;

    if (!ata_wait_ready()) return 0;
    ata_select_and_setup(lba, count);
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    int total = count ? count : 256;
    for (int s = 0; s < total; s++) {
        if (!ata_wait_drq()) return 0;
        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA, src[s * 256 + i]);
        }
    }

    if (!ata_wait_ready()) return 0;
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    if (!ata_wait_ready()) return 0;

    return 1;
}
