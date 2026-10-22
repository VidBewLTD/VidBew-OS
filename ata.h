#ifndef ATA_H
#define ATA_H

/* Returns 1 on success, 0 on failure. buf must hold count*512 bytes. */
int ata_read_sectors(unsigned int lba, unsigned char count, void *buf);
int ata_write_sectors(unsigned int lba, unsigned char count, const void *buf);

#endif
