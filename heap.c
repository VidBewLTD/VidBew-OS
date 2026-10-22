/* heap.c -- a small first-fit, coalescing free-list allocator.
 *
 * The region is handed in by kmain: the bootloader asks the BIOS how much
 * memory sits above 1MB and the kernel claims most of it (capped), so
 * full-screen window buffers at higher resolutions have room. All entry
 * points run with interrupts off for their (short) duration, which is what
 * makes them safe to call from any task now that the scheduler preempts. */

#include "heap.h"
#include "serial.h"
#include "sync.h"
#include "kstd.h"

typedef struct block_header {
    unsigned int size;   /* usable bytes following this header */
    int free;
    struct block_header *next;
} block_header_t;

static block_header_t *heap_head = 0;
static unsigned int heap_total = 0;

void heap_init(unsigned int start, unsigned int size)
{
    heap_head = (block_header_t *)start;
    heap_head->size = size - sizeof(block_header_t);
    heap_head->free = 1;
    heap_head->next = 0;
    heap_total = size;
}

static void coalesce(void)
{
    block_header_t *b = heap_head;
    while (b && b->next) {
        if (b->free && b->next->free) {
            b->size += sizeof(block_header_t) + b->next->size;
            b->next = b->next->next;
        } else {
            b = b->next;
        }
    }
}

void *kmalloc(unsigned int size)
{
    if (size == 0) return 0;
    size = (size + 15u) & ~15u; /* 16-byte granularity keeps payloads nicely aligned */

    u32 f = irq_save();
    block_header_t *b = heap_head;
    while (b) {
        if (b->free && b->size >= size) {
            unsigned int remaining = b->size - size;
            if (remaining > sizeof(block_header_t) + 16) {
                block_header_t *nb = (block_header_t *)((unsigned char *)b + sizeof(block_header_t) + size);
                nb->size = remaining - sizeof(block_header_t);
                nb->free = 1;
                nb->next = b->next;
                b->size = size;
                b->next = nb;
            }
            b->free = 0;
            irq_restore(f);
            return (void *)((unsigned char *)b + sizeof(block_header_t));
        }
        b = b->next;
    }
    irq_restore(f);
    return 0; /* out of memory */
}

void *kzalloc(unsigned int size)
{
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr) return;
    u32 f = irq_save();
    block_header_t *b = (block_header_t *)((unsigned char *)ptr - sizeof(block_header_t));
    if (!b->free) {
        b->free = 1;
        coalesce();
    }
    irq_restore(f);
}

void heap_stats(unsigned int *used, unsigned int *free_bytes, unsigned int *total)
{
    unsigned int u = 0, fr = 0;
    u32 f = irq_save();
    for (block_header_t *b = heap_head; b; b = b->next) {
        if (b->free) fr += b->size; else u += b->size;
    }
    irq_restore(f);
    if (used) *used = u;
    if (free_bytes) *free_bytes = fr;
    if (total) *total = heap_total;
}

void heap_selftest(void)
{
    serial_print("heap: running self-test...\n");

    void *a = kmalloc(64);
    void *b = kmalloc(128);
    void *c = kmalloc(32);

    if (!a || !b || !c || a == b || b == c || a == c) {
        serial_print("heap: FAIL (bad initial allocations)\n");
        return;
    }

    kfree(b);
    void *d = kmalloc(100); /* should reuse the space just freed by b */
    if (!d) {
        serial_print("heap: FAIL (reuse allocation failed)\n");
        return;
    }

    kfree(a);
    kfree(c);
    kfree(d);

    serial_print("heap: self-test PASSED\n");
}
