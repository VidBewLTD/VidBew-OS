#ifndef TIMER_H
#define TIMER_H

#define TIMER_HZ 100

extern volatile unsigned long timer_ticks;

void timer_init(unsigned int frequency_hz);
void timer_handler_c(void);

#endif
