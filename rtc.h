#ifndef RTC_H
#define RTC_H

void rtc_read_time(int *hour, int *minute, int *second);
void rtc_read_date(int *year, int *month, int *day);
void rtc_set_time(int hour, int minute, int second);
void rtc_set_date(int year, int month, int day);
int  rtc_days_in_month(int year, int month);

#endif
