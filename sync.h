#ifndef SYNC_H
#define SYNC_H

#include "types.h"

/* Everything runs on one CPU, so "atomic" just means "interrupts off". */
static inline u32 irq_save(void)
{
    u32 flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static inline void irq_restore(u32 flags)
{
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

/* A recursive mutex that yields the CPU while contended. Safe to take from
 * any task; never take one from an interrupt handler. If the owner dies
 * while holding it, the next locker steals it (see mutex_lock). */
typedef struct {
    volatile int locked;
    volatile int owner;
    int depth;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
void mutex_release_all_owned_by(int task_id);

#endif
