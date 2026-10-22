/* rtc.c -- reads and sets the CMOS Real-Time Clock.
 *
 * Two ports: 0x70 selects a register, 0x71 reads/writes it. Status Register B
 * (0x0B) says whether values are BCD or binary and whether hours are 12- or
 * 24-hour -- both vary by firmware, so we check rather than assume. */

#include "rtc.h"
#include "io.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static unsigned char cmos_read(unsigned char reg)
{
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static void cmos_write(unsigned char reg, unsigned char val)
{
    outb(CMOS_INDEX, reg);
    outb(CMOS_DATA, val);
}

static unsigned char bcd_to_bin(unsigned char val)
{
    return (unsigned char)((val & 0x0F) + ((val / 16) * 10));
}

static unsigned char bin_to_bcd(unsigned char val)
{
    return (unsigned char)(((val / 10) << 4) | (val % 10));
}

static void wait_no_update(void)
{
    int guard = 100000;
    while ((cmos_read(0x0A) & 0x80) && --guard);
}

void rtc_read_time(int *hour, int *minute, int *second)
{
    unsigned char sec, min, hr, status_b;

    wait_no_update();

    sec = cmos_read(0x00);
    min = cmos_read(0x02);
    hr  = cmos_read(0x04);
    status_b = cmos_read(0x0B);

    if (!(status_b & 0x04)) { /* BCD mode */
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr  = (unsigned char)(bcd_to_bin(hr & 0x7F) | (hr & 0x80));
    }
    if (!(status_b & 0x02) && (hr & 0x80)) { /* 12-hour mode, PM flag set */
        hr = (unsigned char)(((hr & 0x7F) % 12) + 12);
    } else if (!(status_b & 0x02)) {
        hr = (unsigned char)((hr & 0x7F) % 12);
    }

    *hour = hr;
    *minute = min;
    *second = sec;
}

void rtc_read_date(int *year, int *month, int *day)
{
    wait_no_update();
    unsigned char d = cmos_read(0x07), m = cmos_read(0x08), y = cmos_read(0x09);
    unsigned char c = cmos_read(0x32);
    unsigned char status_b = cmos_read(0x0B);
    if (!(status_b & 0x04)) {
        d = bcd_to_bin(d); m = bcd_to_bin(m); y = bcd_to_bin(y); c = bcd_to_bin(c);
    }
    if (c < 19 || c > 21) c = 20;
    *day = d;
    *month = m;
    *year = c * 100 + y;
}

int rtc_days_in_month(int year, int month)
{
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return dim[month - 1];
}

void rtc_set_time(int hour, int minute, int second)
{
    unsigned char status_b = cmos_read(0x0B);
    int bcd = !(status_b & 0x04);
    int h12 = !(status_b & 0x02);
    unsigned char hr = (unsigned char)hour;
    if (h12) {
        unsigned char pm = hour >= 12 ? 0x80 : 0;
        int h = hour % 12;
        hr = (unsigned char)(h == 0 ? 12 : h);
        if (bcd) hr = bin_to_bcd(hr);
        hr |= pm;
    } else if (bcd) {
        hr = bin_to_bcd(hr);
    }
    cmos_write(0x0B, status_b | 0x80);          /* halt updates while we write */
    cmos_write(0x00, bcd ? bin_to_bcd((unsigned char)second) : (unsigned char)second);
    cmos_write(0x02, bcd ? bin_to_bcd((unsigned char)minute) : (unsigned char)minute);
    cmos_write(0x04, hr);
    cmos_write(0x0B, status_b & (unsigned char)~0x80);
}

void rtc_set_date(int year, int month, int day)
{
    unsigned char status_b = cmos_read(0x0B);
    int bcd = !(status_b & 0x04);
    unsigned char y = (unsigned char)(year % 100), c = (unsigned char)(year / 100);
    unsigned char m = (unsigned char)month, d = (unsigned char)day;
    if (bcd) { y = bin_to_bcd(y); c = bin_to_bcd(c); m = bin_to_bcd(m); d = bin_to_bcd(d); }
    cmos_write(0x0B, status_b | 0x80);
    cmos_write(0x07, d);
    cmos_write(0x08, m);
    cmos_write(0x09, y);
    cmos_write(0x32, c);
    cmos_write(0x0B, status_b & (unsigned char)~0x80);
}
