#ifndef FAT16_H
#define FAT16_H

/* FAT16, root directory only, 8.3 names, read + write + delete. Every call
 * takes the filesystem lock internally, so any task may use it. */

typedef struct {
    char name[13];            /* "NAME.EXT" */
    unsigned int size;
    unsigned short date, time; /* raw FAT date/time words (0 = unset) */
    unsigned char attr;       /* 0x10 = directory */
} fat16_entry_t;

int fat16_init(unsigned int partition_start_lba);
int fat16_read_file(const char *name, unsigned char *buf, unsigned int buf_size, unsigned int *out_size);
int fat16_write_file(const char *name, const unsigned char *data, unsigned int size); /* replaces an existing file */
int fat16_delete_file(const char *name);
int fat16_read_dir(fat16_entry_t *out, int max);          /* returns entry count */
unsigned int fat16_free_bytes(void);

/* raw access for the settings sector (shares the same lock) */
int fat16_raw_read(unsigned int lba, void *buf);
int fat16_raw_write(unsigned int lba, const void *buf);

#endif
