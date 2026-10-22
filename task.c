/* task.c -- preemptive round-robin scheduler with sleeping and cleanup.
 *
 * switch_task() (switch_task.asm) swaps stacks. The timer interrupt calls
 * schedule() every tick; tasks can also give up the CPU voluntarily with
 * task_yield()/task_sleep(). The UI task (id 0) never sleeps -- it parks in
 * `hlt` between events and flags itself idle, so time spent there is counted
 * as idle rather than as CPU use. */

#include "task.h"
#include "heap.h"
#include "timer.h"
#include "sync.h"
#include "kstd.h"
#include "serial.h"

extern void switch_task(unsigned int *old_esp_store, unsigned int new_esp);

#define SLICE_TICKS 2

typedef struct {
    unsigned int esp;
    unsigned char *stack;
    int state;
    int needs_cleanup;        /* died without running the exit hook */
    unsigned long wake_tick;
    unsigned long cpu_ticks;
    char name[16];
} task_t;

static task_t tasks[MAX_TASKS];
static int current = 0;
static int slice_left = SLICE_TICKS;
static volatile int ui_idle = 0;
static unsigned long idle_ticks = 0;
static unsigned long total_ticks = 0;

void (*task_exit_hook)(int id) = 0;

void tasks_init(void)
{
    memset(tasks, 0, sizeof(tasks));
    tasks[0].state = TASK_READY;
    kstrlcpy(tasks[0].name, "ui", sizeof(tasks[0].name));
    current = 0;
}

static void task_exit_tramp(void)
{
    task_exit();
}

int task_create(const char *name, task_fn_t fn, void *arg, unsigned int stack_size)
{
    if (!stack_size) stack_size = TASK_STACK_DEFAULT;
    unsigned char *stack = (unsigned char *)kmalloc(stack_size);
    if (!stack) return -1;

    u32 f = irq_save();
    int id = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) { id = i; break; }
    }
    if (id < 0) {
        irq_restore(f);
        kfree(stack);
        return -1;
    }

    task_t *t = &tasks[id];
    unsigned int *sp = (unsigned int *)(stack + stack_size);
    *--sp = (unsigned int)arg;              /* fn's first argument */
    *--sp = (unsigned int)task_exit_tramp;  /* fn's return address */
    *--sp = (unsigned int)fn;               /* popped by switch_task's ret */
    *--sp = 0;                              /* ebp */
    for (int i = 0; i < 8; i++) *--sp = 0;  /* pushad frame */

    t->esp = (unsigned int)sp;
    t->stack = stack;
    t->state = TASK_READY;
    t->needs_cleanup = 0;
    t->cpu_ticks = 0;
    t->wake_tick = 0;
    kstrlcpy(t->name, name ? name : "task", sizeof(t->name));
    irq_restore(f);
    return id;
}

/* Pick the next runnable task after `current`, round-robin. Wakes sleepers
 * whose time has come. Returns `current` if nothing else is runnable. */
static int pick_next(void)
{
    for (int k = 1; k <= MAX_TASKS; k++) {
        int i = (current + k) % MAX_TASKS;
        task_t *t = &tasks[i];
        if (t->state == TASK_SLEEPING && (long)(timer_ticks - t->wake_tick) >= 0)
            t->state = TASK_READY;
        if (t->state == TASK_READY) return i;
    }
    return current;
}

/* interrupts must be off */
static void do_switch(void)
{
    int next = pick_next();
    if (next == current) return;
    int prev = current;
    current = next;
    slice_left = SLICE_TICKS;
    switch_task(&tasks[prev].esp, tasks[next].esp);
}

void schedule(void)
{
    total_ticks++;
    if (current == 0 && ui_idle) idle_ticks++;
    else tasks[current].cpu_ticks++;

    if ((current == 0 && ui_idle) || tasks[current].state != TASK_READY) {
        do_switch();
        return;
    }
    if (--slice_left > 0) return;
    slice_left = SLICE_TICKS;
    do_switch();
}

void task_yield(void)
{
    u32 f = irq_save();
    do_switch();
    irq_restore(f);
}

void task_sleep(unsigned int ticks)
{
    if (ticks == 0) ticks = 1;
    u32 f = irq_save();
    tasks[current].wake_tick = timer_ticks + ticks;
    tasks[current].state = TASK_SLEEPING;
    do_switch();
    irq_restore(f);
}

void task_wake(int id)
{
    if (id <= 0 || id >= MAX_TASKS) return;
    u32 f = irq_save();
    if (tasks[id].state == TASK_SLEEPING) tasks[id].state = TASK_READY;
    irq_restore(f);
}

void task_exit(void)
{
    int id = current;
    if (task_exit_hook) task_exit_hook(id);
    mutex_release_all_owned_by(id);
    irq_save();
    tasks[id].needs_cleanup = 0;
    tasks[id].state = TASK_DEAD;
    for (;;) {
        do_switch();
        __asm__ volatile("sti; hlt");
        __asm__ volatile("cli");
    }
}

/* Called from the CPU-exception handler: the task has faulted and must not
 * run again. We only mark it; the UI task does the (lock-taking) cleanup. */
void task_crash_current(void)
{
    tasks[current].needs_cleanup = 1;
    tasks[current].state = TASK_DEAD;
    for (;;) {
        do_switch();
        __asm__ volatile("sti; hlt");
        __asm__ volatile("cli");
    }
}

void task_kill(int id)
{
    if (id <= 0 || id >= MAX_TASKS) return;
    if (id == current) task_exit();
    if (tasks[id].state == TASK_FREE || tasks[id].state == TASK_DEAD) return;
    u32 f = irq_save();
    tasks[id].needs_cleanup = 1;
    tasks[id].state = TASK_DEAD;
    irq_restore(f);
}

void task_reap(void)
{
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_DEAD || i == current) continue;
        if (tasks[i].needs_cleanup) {
            tasks[i].needs_cleanup = 0;
            serial_print("task: reaping ");
            serial_print(tasks[i].name);
            serial_print("\n");
            if (task_exit_hook) task_exit_hook(i);
            mutex_release_all_owned_by(i);
        }
        u32 f = irq_save();
        unsigned char *stack = tasks[i].stack;
        tasks[i].stack = 0;
        tasks[i].state = TASK_FREE;
        irq_restore(f);
        if (stack) kfree(stack);
    }
}

int task_current(void) { return current; }
int task_state(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].state : TASK_FREE; }
const char *task_name(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].name : ""; }
unsigned long task_cpu_ticks(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].cpu_ticks : 0; }
unsigned long task_idle_ticks(void) { return idle_ticks; }
unsigned long task_total_ticks(void) { return total_ticks; }
void task_set_ui_idle(int idle) { ui_idle = idle; }

int task_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_SLEEPING) n++;
    return n;
}
