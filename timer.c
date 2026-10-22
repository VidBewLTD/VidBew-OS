/* timer.c -- Programmable Interval Timer driver (IRQ0).
 *
 * Counts ticks (the OS's heartbeat) and drives the scheduler: every tick
 * ends in schedule(), which decides whether to switch tasks. */

#include "timer.h"
#include "io.h"
#include "task.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQUENCY 1193182u

volatile unsigned long timer_ticks = 0;

void timer_init(unsigned int frequency_hz)
{
    unsigned int divisor = PIT_BASE_FREQUENCY / frequency_hz;

    outb(PIT_COMMAND, 0x36); /* channel 0, lobyte/hibyte, mode 3 (square wave) */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

void timer_handler_c(void)
{
    timer_ticks++;
    outb(0x20, 0x20); /* EOI to master PIC -- must happen before we
                       * potentially switch away, since we may not
                       * come back to this exact call for some time */
    schedule();
}
