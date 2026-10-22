/* idt.c -- Interrupt Descriptor Table, 8259 PIC setup and CPU exceptions.
 *
 * The PIC is remapped so IRQs land at 0x20-0x2F (the defaults collide with
 * CPU exceptions) and everything but the timer (IRQ0), keyboard (IRQ1), the
 * cascade (IRQ2) and mouse (IRQ12) is masked. The network card is polled, so
 * it needs no IRQ line.
 *
 * CPU exceptions 0-19 land in exception_c(): a fault inside an app task
 * kills just that task (its windows are cleaned up by the UI task) instead
 * of taking the whole OS down; a fault in the UI task itself is fatal. */

#include "idt.h"
#include "io.h"
#include "gfx.h"
#include "task.h"
#include "kstd.h"
#include "serial.h"

struct idt_entry {
    unsigned short base_low;
    unsigned short selector;
    unsigned char  always0;
    unsigned char  flags;
    unsigned short base_high;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

#define IDT_ENTRIES 256
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

#define KERNEL_CODE_SEGMENT 0x08   /* must match the GDT in stage2.asm */

extern void idt_load(unsigned int idt_ptr_addr);
extern void default_isr_stub(void);
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);
extern unsigned int exc_table[20];

static const char *exc_names[20] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow", "bound range", "invalid opcode",
    "device not available", "double fault", "coprocessor overrun", "invalid TSS", "segment not present",
    "stack fault", "general protection fault", "page fault", "reserved", "x87 error",
    "alignment check", "machine check", "SIMD error"
};

void idt_set_gate(int n, unsigned int handler)
{
    idt[n].base_low  = handler & 0xFFFF;
    idt[n].base_high = (handler >> 16) & 0xFFFF;
    idt[n].selector  = KERNEL_CODE_SEGMENT;
    idt[n].always0   = 0;
    idt[n].flags     = 0x8E; /* present, ring0, 32-bit interrupt gate */
}

/* Anything we never asked for (e.g. a spurious IRQ7): acknowledge and ignore. */
void unhandled_interrupt_c(void)
{
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void exception_c(unsigned int *frame)
{
    unsigned int vec = frame[8], err = frame[9], eip = frame[10];
    int id = task_current();
    char msg[120];
    ksnprintf(msg, sizeof(msg), "EXCEPTION %u (%s) err=%x eip=%x in task %d '%s'\n",
              vec, vec < 20 ? exc_names[vec] : "?", err, eip, id, task_name(id));
    serial_print(msg);

    if (id > 0) task_crash_current();   /* never returns */

    ksnprintf(msg, sizeof(msg), "KERNEL PANIC: %s (eip %x, err %x)",
              vec < 20 ? exc_names[vec] : "exception", eip, err);
    gfx_panic(msg);
    for (;;) __asm__ volatile("cli; hlt");
}

void pic_remap_and_mask(void)
{
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);   /* master -> 0x20 */
    outb(0xA1, 0x28);   /* slave  -> 0x28 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /* master: timer (IRQ0), keyboard (IRQ1) and the cascade line (IRQ2)
     * that carries slave interrupts; slave: mouse (IRQ12) only. */
    outb(0x21, (unsigned char)(0xFF & ~(1 << 0) & ~(1 << 1) & ~(1 << 2)));
    outb(0xA1, (unsigned char)(0xFF & ~(1 << 4)));
}

void idt_init(void)
{
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (unsigned int)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++)
        idt_set_gate(i, (unsigned int)default_isr_stub);
    for (int i = 0; i < 20; i++)
        idt_set_gate(i, exc_table[i]);

    idt_set_gate(0x20, (unsigned int)irq0_stub);
    idt_set_gate(0x21, (unsigned int)irq1_stub);
    idt_set_gate(0x2C, (unsigned int)irq12_stub);

    idt_load((unsigned int)&idtp);
    pic_remap_and_mask();
}
