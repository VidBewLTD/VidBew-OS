#ifndef HEAP_H
#define HEAP_H

void heap_init(unsigned int start, unsigned int size);
void *kmalloc(unsigned int size);
void *kzalloc(unsigned int size);
void kfree(void *ptr);
void heap_stats(unsigned int *used, unsigned int *free_bytes, unsigned int *total);
void heap_selftest(void);

#endif
