#ifndef IDT_H
#define IDT_H

void idt_init(void);
void idt_set_gate(int n, unsigned int handler);
void pic_remap_and_mask(void);

#endif
