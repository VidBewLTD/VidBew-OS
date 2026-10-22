/* sync.c -- cooperative mutexes for the preemptive scheduler. */

#include "sync.h"
#include "task.h"

#define MAX_MUTEXES 16
static mutex_t *registry[MAX_MUTEXES];
static int registry_count = 0;

void mutex_init(mutex_t *m)
{
    m->locked = 0;
    m->owner = -1;
    m->depth = 0;
    u32 f = irq_save();
    for (int i = 0; i < registry_count; i++)
        if (registry[i] == m) { irq_restore(f); return; }
    if (registry_count < MAX_MUTEXES) registry[registry_count++] = m;
    irq_restore(f);
}

void mutex_lock(mutex_t *m)
{
    int me = task_current();
    for (;;) {
        u32 f = irq_save();
        if (m->locked && m->owner != me && m->owner >= 0 && task_state(m->owner) == TASK_DEAD) {
            /* owner crashed or was killed while holding it: take it over */
            m->locked = 0;
            m->owner = -1;
            m->depth = 0;
        }
        if (!m->locked) {
            m->locked = 1;
            m->owner = me;
            m->depth = 1;
            irq_restore(f);
            return;
        }
        if (m->owner == me) {
            m->depth++;
            irq_restore(f);
            return;
        }
        irq_restore(f);
        task_yield();
    }
}

void mutex_unlock(mutex_t *m)
{
    u32 f = irq_save();
    if (m->locked && m->owner == task_current()) {
        if (--m->depth <= 0) {
            m->depth = 0;
            m->locked = 0;
            m->owner = -1;
        }
    }
    irq_restore(f);
}

void mutex_release_all_owned_by(int task_id)
{
    u32 f = irq_save();
    for (int i = 0; i < registry_count; i++) {
        mutex_t *m = registry[i];
        if (m->locked && m->owner == task_id) {
            m->locked = 0;
            m->owner = -1;
            m->depth = 0;
        }
    }
    irq_restore(f);
}
