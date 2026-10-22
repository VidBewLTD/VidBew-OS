#ifndef PCI_H
#define PCI_H

#include "types.h"

typedef struct {
    u8  bus, slot, func;
    u16 vendor, device;
    u32 bar[6];
} pci_dev_t;

u32  pci_read32(u8 bus, u8 slot, u8 func, u8 off);
void pci_write32(u8 bus, u8 slot, u8 func, u8 off, u32 val);
int  pci_find(const u16 ids[][2], int count, pci_dev_t *out);   /* first device matching any {vendor, device} */
void pci_enable(const pci_dev_t *d, int io, int mem, int bus_master);

#endif
