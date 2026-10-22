/* fat16.c -- a real, if intentionally scoped, FAT16 driver.
 *
 * Scope: root directory only (no subdirectories yet), 8.3 filenames, read +
 * write + delete. The on-disk format is the full real thing, created with
 * the standard `mkfs.fat` tool and verified against `mtools` on the build
 * side. All entry points hold one filesystem mutex, because several windows
 * (Explorer, Terminal, Control Panel...) now run as separate tasks. */

#include "fat16.h"
#include "ata.h"
#include "sync.h"
#include "kstd.h"
#include "rtc.h"

struct fat_dirent {
    char name[11];
    unsigned char attr;
    unsigned char reserved;
    unsigned char create_time_tenth;
    unsigned short create_time;
    unsigned short create_date;
    unsigned short access_date;
    unsigned short first_cluster_high;
    unsigned short write_time;
    unsigned short write_date;
    unsigned short first_cluster_low;
    unsigned int file_size;
} __attribute__((packed));

static mutex_t fs_mutex;
static unsigned int partition_lba;
static unsigned int bytes_per_sector;
static unsigned int sectors_per_cluster;
static unsigned int reserved_sectors;
static unsigned int num_fats;
static unsigned int root_entry_count;
static unsigned int sectors_per_fat;
static unsigned int fat_start_lba;
static unsigned int root_dir_start_lba;
static unsigned int root_dir_sectors;
static unsigned int data_start_lba;
static unsigned int total_clusters;

int fat16_init(unsigned int part_lba)
{
    mutex_init(&fs_mutex);
    unsigned char sector[512];
    if (!ata_read_sectors(part_lba, 1, sector)) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;

    partition_lba       = part_lba;
    bytes_per_sector    = (unsigned int)sector[11] | ((unsigned int)sector[12] << 8);
    sectors_per_cluster = sector[13];
    reserved_sectors    = (unsigned int)sector[14] | ((unsigned int)sector[15] << 8);
    num_fats            = sector[16];
    root_entry_count    = (unsigned int)sector[17] | ((unsigned int)sector[18] << 8);
    sectors_per_fat     = (unsigned int)sector[22] | ((unsigned int)sector[23] << 8);

    unsigned int total_sectors_16 = (unsigned int)sector[19] | ((unsigned int)sector[20] << 8);
    unsigned int total_sectors_32 = (unsigned int)sector[32] | ((unsigned int)sector[33] << 8) |
                                     ((unsigned int)sector[34] << 16) | ((unsigned int)sector[35] << 24);
    unsigned int total_sectors = total_sectors_16 ? total_sectors_16 : total_sectors_32;

    if (bytes_per_sector != 512) return 0; /* driver assumes 512-byte sectors */

    fat_start_lba = partition_lba + reserved_sectors;
    root_dir_start_lba = fat_start_lba + num_fats * sectors_per_fat;
    root_dir_sectors = (root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;
    data_start_lba = root_dir_start_lba + root_dir_sectors;

    unsigned int data_sectors = total_sectors - (reserved_sectors + num_fats * sectors_per_fat + root_dir_sectors);
    total_clusters = data_sectors / sectors_per_cluster;
    return 1;
}

static unsigned int cluster_to_lba(unsigned int cluster)
{
    return data_start_lba + (cluster - 2) * sectors_per_cluster;
}

static unsigned short fat_read_entry(unsigned int cluster)
{
    unsigned int fat_offset = cluster * 2;
    unsigned int sector = fat_start_lba + (fat_offset / bytes_per_sector);
    unsigned int off = fat_offset % bytes_per_sector;
    unsigned char buf[512];
    ata_read_sectors(sector, 1, buf);
    return (unsigned short)(buf[off] | (buf[off + 1] << 8));
}

static void fat_write_entry(unsigned int cluster, unsigned short value)
{
    unsigned int fat_offset = cluster * 2;
    unsigned int sector_in_fat = fat_offset / bytes_per_sector;
    unsigned int off = fat_offset % bytes_per_sector;
    unsigned char buf[512];

    for (unsigned int copy = 0; copy < num_fats; copy++) {
        unsigned int sector = fat_start_lba + copy * sectors_per_fat + sector_in_fat;
        ata_read_sectors(sector, 1, buf);
        buf[off] = value & 0xFF;
        buf[off + 1] = (value >> 8) & 0xFF;
        ata_write_sectors(sector, 1, buf);
    }
}

/* Scan the FAT a sector (256 entries) at a time instead of one read per entry. */
static unsigned int fat_find_free_cluster(void)
{
    unsigned char buf[512];
    unsigned int per_sector = bytes_per_sector / 2;
    for (unsigned int s = 0; s < sectors_per_fat; s++) {
        ata_read_sectors(fat_start_lba + s, 1, buf);
        for (unsigned int i = 0; i < per_sector; i++) {
            unsigned int c = s * per_sector + i;
            if (c < 2) continue;
            if (c >= total_clusters + 2) return 0;
            if ((buf[i * 2] | (buf[i * 2 + 1] << 8)) == 0) return c;
        }
    }
    return 0;
}

static void free_chain(unsigned int cluster)
{
    int guard = 70000;
    while (cluster >= 2 && cluster < 0xFFF8 && guard--) {
        unsigned int next = fat_read_entry(cluster);
        fat_write_entry(cluster, 0);
        cluster = next;
    }
}

static void name_to_83(const char *input, char *out11)
{
    for (int i = 0; i < 11; i++) out11[i] = ' ';

    int i = 0, j = 0;
    while (input[i] && input[i] != '.' && j < 8) {
        out11[j++] = kupper(input[i]);
        i++;
    }
    while (input[i] && input[i] != '.') i++;
    if (input[i] == '.') {
        i++;
        int k = 8;
        while (input[i] && k < 11) {
            out11[k++] = kupper(input[i]);
            i++;
        }
    }
}

static void format_name(const char *name11, char *out13)
{
    int n = 0;
    for (int i = 0; i < 8 && name11[i] != ' '; i++) out13[n++] = name11[i];
    if (name11[8] != ' ') {
        out13[n++] = '.';
        for (int i = 8; i < 11 && name11[i] != ' '; i++) out13[n++] = name11[i];
    }
    out13[n] = 0;
}

static int find_dirent(const char *name11, struct fat_dirent *out,
                       unsigned int *sector_out, unsigned int *offset_out)
{
    unsigned char buf[512];
    for (unsigned int s = 0; s < root_dir_sectors; s++) {
        unsigned int lba = root_dir_start_lba + s;
        ata_read_sectors(lba, 1, buf);
        for (int e = 0; e < 512 / 32; e++) {
            struct fat_dirent *de = (struct fat_dirent *)(buf + e * 32);
            if (de->name[0] == 0x00) return 0;
            if ((unsigned char)de->name[0] == 0xE5) continue;
            if (de->attr == 0x0F) continue; /* long-filename entry, unsupported */
            if (memcmp(de->name, name11, 11) == 0) {
                *out = *de;
                if (sector_out) *sector_out = lba;
                if (offset_out) *offset_out = (unsigned int)(e * 32);
                return 1;
            }
        }
    }
    return 0;
}

static int alloc_dir_slot(unsigned int *sector_out, unsigned int *offset_out)
{
    unsigned char buf[512];
    for (unsigned int s = 0; s < root_dir_sectors; s++) {
        unsigned int lba = root_dir_start_lba + s;
        ata_read_sectors(lba, 1, buf);
        for (int e = 0; e < 512 / 32; e++) {
            unsigned char first_byte = buf[e * 32];
            if (first_byte == 0x00 || first_byte == 0xE5) {
                *sector_out = lba;
                *offset_out = (unsigned int)(e * 32);
                return 1;
            }
        }
    }
    return 0;
}

static void fat_now(unsigned short *date, unsigned short *time)
{
    int y, mo, d, h, mi, s;
    rtc_read_date(&y, &mo, &d);
    rtc_read_time(&h, &mi, &s);
    if (y < 1980) y = 1980;
    *date = (unsigned short)(((y - 1980) << 9) | (mo << 5) | d);
    *time = (unsigned short)((h << 11) | (mi << 5) | (s / 2));
}

int fat16_read_file(const char *name, unsigned char *buf, unsigned int buf_size, unsigned int *out_size)
{
    mutex_lock(&fs_mutex);
    char name11[11];
    name_to_83(name, name11);

    struct fat_dirent de;
    if (!find_dirent(name11, &de, 0, 0)) { mutex_unlock(&fs_mutex); return 0; }

    unsigned int file_size = de.file_size;
    unsigned int cluster = de.first_cluster_low;
    unsigned int written = 0;
    unsigned char sector_buf[512];
    int guard = 70000;

    while (cluster >= 2 && cluster < 0xFFF8 && written < file_size && guard--) {
        unsigned int lba = cluster_to_lba(cluster);
        for (unsigned int s = 0; s < sectors_per_cluster && written < file_size; s++) {
            ata_read_sectors(lba + s, 1, sector_buf);

            unsigned int to_copy = file_size - written;
            if (to_copy > bytes_per_sector) to_copy = bytes_per_sector;
            if (written + to_copy > buf_size)
                to_copy = (buf_size > written) ? (buf_size - written) : 0;
            for (unsigned int i = 0; i < to_copy; i++) buf[written + i] = sector_buf[i];
            written += to_copy;
        }
        cluster = fat_read_entry(cluster);
    }

    if (out_size) *out_size = written;
    mutex_unlock(&fs_mutex);
    return 1;
}

int fat16_write_file(const char *name, const unsigned char *data, unsigned int size)
{
    mutex_lock(&fs_mutex);
    char name11[11];
    name_to_83(name, name11);

    struct fat_dirent old;
    unsigned int dir_sector = 0, dir_offset = 0;
    int exists = find_dirent(name11, &old, &dir_sector, &dir_offset);
    unsigned int old_first = exists ? old.first_cluster_low : 0;

    unsigned int cluster_bytes = sectors_per_cluster * bytes_per_sector;
    unsigned int clusters_needed = (size + cluster_bytes - 1) / cluster_bytes;
    if (clusters_needed == 0) clusters_needed = 1;

    unsigned int first_cluster = 0, prev_cluster = 0, written = 0;
    unsigned char sector_buf[512];

    for (unsigned int i = 0; i < clusters_needed; i++) {
        unsigned int c = fat_find_free_cluster();
        if (!c) {                       /* disk full: give back what we took, old file untouched */
            if (first_cluster) free_chain(first_cluster);
            mutex_unlock(&fs_mutex);
            return 0;
        }
        fat_write_entry(c, 0xFFFF);     /* tentative end-of-chain */
        if (i == 0) first_cluster = c;
        if (prev_cluster) fat_write_entry(prev_cluster, (unsigned short)c);
        prev_cluster = c;

        unsigned int lba = cluster_to_lba(c);
        for (unsigned int s = 0; s < sectors_per_cluster; s++) {
            for (int b = 0; b < 512; b++) {
                sector_buf[b] = (written < size) ? data[written++] : 0;
            }
            ata_write_sectors(lba + s, 1, sector_buf);
        }
    }

    if (!exists && !alloc_dir_slot(&dir_sector, &dir_offset)) {
        free_chain(first_cluster);
        mutex_unlock(&fs_mutex);
        return 0;
    }

    unsigned char buf[512];
    ata_read_sectors(dir_sector, 1, buf);
    struct fat_dirent *de = (struct fat_dirent *)(buf + dir_offset);
    unsigned short date, time;
    fat_now(&date, &time);

    memcpy(de->name, name11, 11);
    de->attr = 0x20;
    de->reserved = 0;
    de->create_time_tenth = 0;
    de->create_time = time;
    de->create_date = date;
    de->access_date = date;
    de->first_cluster_high = 0;
    de->write_time = time;
    de->write_date = date;
    de->first_cluster_low = (unsigned short)first_cluster;
    de->file_size = size;
    ata_write_sectors(dir_sector, 1, buf);

    if (old_first) free_chain(old_first);   /* only now is the old copy really gone */
    mutex_unlock(&fs_mutex);
    return 1;
}

int fat16_delete_file(const char *name)
{
    mutex_lock(&fs_mutex);
    char name11[11];
    name_to_83(name, name11);
    struct fat_dirent de;
    unsigned int sector, offset;
    if (!find_dirent(name11, &de, &sector, &offset) || (de.attr & 0x10)) {
        mutex_unlock(&fs_mutex);
        return 0;
    }
    unsigned char buf[512];
    ata_read_sectors(sector, 1, buf);
    buf[offset] = 0xE5;
    ata_write_sectors(sector, 1, buf);
    if (de.first_cluster_low >= 2) free_chain(de.first_cluster_low);
    mutex_unlock(&fs_mutex);
    return 1;
}

int fat16_read_dir(fat16_entry_t *out, int max)
{
    mutex_lock(&fs_mutex);
    unsigned char buf[512];
    int count = 0;
    for (unsigned int s = 0; s < root_dir_sectors && count < max; s++) {
        ata_read_sectors(root_dir_start_lba + s, 1, buf);
        for (int e = 0; e < 512 / 32 && count < max; e++) {
            struct fat_dirent *de = (struct fat_dirent *)(buf + e * 32);
            if (de->name[0] == 0x00) { mutex_unlock(&fs_mutex); return count; }
            if ((unsigned char)de->name[0] == 0xE5) continue;
            if (de->attr == 0x0F || (de->attr & 0x08)) continue;
            format_name(de->name, out[count].name);
            out[count].size = de->file_size;
            out[count].date = de->write_date;
            out[count].time = de->write_time;
            out[count].attr = de->attr;
            count++;
        }
    }
    mutex_unlock(&fs_mutex);
    return count;
}

unsigned int fat16_free_bytes(void)
{
    mutex_lock(&fs_mutex);
    unsigned char buf[512];
    unsigned int per_sector = bytes_per_sector / 2, free_clusters = 0;
    for (unsigned int s = 0; s < sectors_per_fat; s++) {
        ata_read_sectors(fat_start_lba + s, 1, buf);
        for (unsigned int i = 0; i < per_sector; i++) {
            unsigned int c = s * per_sector + i;
            if (c < 2) continue;
            if (c >= total_clusters + 2) break;
            if ((buf[i * 2] | (buf[i * 2 + 1] << 8)) == 0) free_clusters++;
        }
    }
    mutex_unlock(&fs_mutex);
    return free_clusters * sectors_per_cluster * bytes_per_sector;
}

int fat16_raw_read(unsigned int lba, void *buf)
{
    mutex_lock(&fs_mutex);
    int ok = ata_read_sectors(lba, 1, buf);
    mutex_unlock(&fs_mutex);
    return ok;
}

int fat16_raw_write(unsigned int lba, const void *buf)
{
    mutex_lock(&fs_mutex);
    int ok = ata_write_sectors(lba, 1, buf);
    mutex_unlock(&fs_mutex);
    return ok;
}
