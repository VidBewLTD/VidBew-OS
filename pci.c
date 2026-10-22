/* pci.c -- minimal PCI configuration-space access (mechanism #1) and a
 * bus scan, enough to find a network card and switch on its decoders. */

#include "pci.h"
#include "io.h"

static u32 cfg_addr(u8 bus, u8 slot, u8 func, u8 off)
{
    return 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) | ((u32)func << 8) | (off & 0xFC);
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 off)
{
    outl(0xCF8, cfg_addr(bus, slot, func, off));
    return inl(0xCFC);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 off, u32 val)
{
    outl(0xCF8, cfg_addr(bus, slot, func, off));
    outl(0xCFC, val);
}

int pci_find(const u16 ids[][2], int count, pci_dev_t *out)
{
    for (int bus = 0; bus < 8; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            u32 id0 = pci_read32((u8)bus, (u8)slot, 0, 0);
            if ((id0 & 0xFFFF) == 0xFFFF) continue;
            int multi = (pci_read32((u8)bus, (u8)slot, 0, 0x0C) >> 16) & 0x80;
            for (int func = 0; func < (multi ? 8 : 1); func++) {
                u32 id = pci_read32((u8)bus, (u8)slot, (u8)func, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                u16 ven = (u16)(id & 0xFFFF), dev = (u16)(id >> 16);
                for (int k = 0; k < count; k++) {
                    if (ids[k][0] == ven && ids[k][1] == dev) {
                        out->bus = (u8)bus; out->slot = (u8)slot; out->func = (u8)func;
                        out->vendor = ven; out->device = dev;
                        for (int b = 0; b < 6; b++) out->bar[b] = pci_read32((u8)bus, (u8)slot, (u8)func, (u8)(0x10 + b * 4));
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

void pci_enable(const pci_dev_t *d, int io, int mem, int bus_master)
{
    u32 cmd = pci_read32(d->bus, d->slot, d->func, 0x04) & 0xFFFF;
    if (io) cmd |= 1;
    if (mem) cmd |= 2;
    if (bus_master) cmd |= 4;
    pci_write32(d->bus, d->slot, d->func, 0x04, cmd);   /* status bits (high half) written as 0: untouched */
}
