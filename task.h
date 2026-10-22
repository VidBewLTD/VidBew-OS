#ifndef TASK_H
#define TASK_H

/* Preemptive multitasking, all inside one address space.
 *
 * Task 0 is the kernel's own main loop (the UI task). Every other task is
 * created with task_create() and gets its own stack. Tasks end by returning
 * from their entry function, calling task_exit(), or being task_kill()ed;
 * the UI task reaps their stacks with task_reap(). */

#define MAX_TASKS 24
#define TASK_STACK_DEFAULT 16384

#define TASK_FREE     0
#define TASK_READY    1
#define TASK_SLEEPING 2
#define TASK_DEAD     3

typedef void (*task_fn_t)(void *arg);

void tasks_init(void);
int  task_create(const char *name, task_fn_t fn, void *arg, unsigned int stack_size);
void task_exit(void) __attribute__((noreturn));
void task_yield(void);
void task_sleep(unsigned int ticks);   /* sleep at least 1 tick; task_wake() cuts it short */
void task_wake(int id);
void task_kill(int id);
void task_reap(void);                  /* UI task only: frees dead tasks' stacks */
int  task_current(void);
int  task_state(int id);
const char *task_name(int id);
unsigned long task_cpu_ticks(int id);
unsigned long task_idle_ticks(void);   /* ticks the UI task spent parked in hlt */
unsigned long task_total_ticks(void);
int  task_count(void);
void task_set_ui_idle(int idle);
void task_crash_current(void) __attribute__((noreturn));

/* called (from the UI task or the dying task) so windows owned by a dead task
 * can be cleaned up */
extern void (*task_exit_hook)(int id);

void schedule(void);                   /* called from the timer interrupt */

#endif
