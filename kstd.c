/* kstd.c -- the small set of libc-style helpers the kernel needs. */

#include "kstd.h"

void *memcpy(void *dst, const void *src, unsigned n)
{
    void *ret = dst;
    unsigned q = n >> 2, r = n & 3;
    __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(q) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(r) : : "memory");
    return ret;
}

void *memset(void *dst, int c, unsigned n)
{
    void *ret = dst;
    unsigned v = (unsigned char)c;
    v |= v << 8;
    v |= v << 16;
    unsigned q = n >> 2, r = n & 3;
    __asm__ volatile("rep stosl" : "+D"(dst), "+c"(q) : "a"(v) : "memory");
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(r) : "a"(v) : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, unsigned n)
{
    if (dst == src || n == 0) return dst;
    if ((unsigned)dst < (unsigned)src || (unsigned)dst >= (unsigned)src + n)
        return memcpy(dst, src, n);
    unsigned char *d = (unsigned char *)dst + n - 1;
    const unsigned char *s = (const unsigned char *)src + n - 1;
    unsigned cnt = n;
    __asm__ volatile("std; rep movsb; cld" : "+D"(d), "+S"(s), "+c"(cnt) : : "memory");
    return dst;
}

int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (unsigned i = 0; i < n; i++)
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}

unsigned kstrlen(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, unsigned n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char kupper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
char klower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

int kstricmp(const char *a, const char *b)
{
    while (*a && klower(*a) == klower(*b)) { a++; b++; }
    return (int)(unsigned char)klower(*a) - (int)(unsigned char)klower(*b);
}

void kstrlcpy(char *dst, const char *src, unsigned cap)
{
    if (!cap) return;
    unsigned i = 0;
    while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void kstrlcat(char *dst, const char *src, unsigned cap)
{
    unsigned n = kstrlen(dst);
    if (n + 1 >= cap) return;
    kstrlcpy(dst + n, src, cap - n);
}

int katoi(const char *s)
{
    int neg = 0, v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

int kvsnprintf(char *buf, unsigned cap, const char *fmt, va_list ap)
{
    unsigned n = 0;
#define PUT(ch) do { if (cap && n + 1 < cap) buf[n] = (char)(ch); n++; } while (0)

    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        char tmp[16];
        const char *str = tmp;
        int len = 0, neg = 0;

        switch (*fmt) {
        case 'd': case 'u': case 'x': case 'X': {
            unsigned v;
            unsigned base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
            if (*fmt == 'd') {
                int sv = va_arg(ap, int);
                neg = sv < 0;
                v = neg ? (unsigned)(-(sv + 1)) + 1u : (unsigned)sv;
            } else {
                v = va_arg(ap, unsigned);
            }
            char rev[16];
            int rl = 0;
            if (v == 0) rev[rl++] = '0';
            while (v) {
                unsigned d = v % base;
                rev[rl++] = (char)(d < 10 ? '0' + d : (*fmt == 'X' ? 'A' : 'a') + (d - 10));
                v /= base;
            }
            int pos = 0;
            if (neg && !zero) tmp[pos++] = '-';
            /* when zero-padding a negative number, the sign goes first, then zeros */
            int digits = rl + (neg && zero ? 1 : 0);
            if (neg && zero) {
                PUT('-');
                width--;
                digits = rl;
            }
            while (zero && !left && digits < width && pos < 15) { tmp[pos++] = '0'; digits++; }
            while (rl && pos < 15) tmp[pos++] = rev[--rl];
            len = pos;
            break;
        }
        case 's':
            str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            len = (int)kstrlen(str);
            break;
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case '%':
            PUT('%');
            continue;
        case 0:
            goto done;
        default:
            PUT('%');
            PUT(*fmt);
            continue;
        }

        int pad = width > len ? width - len : 0;
        if (!left) while (pad-- > 0) PUT(' ');
        for (int i = 0; i < len; i++) PUT(str[i]);
        if (left) while (pad-- > 0) PUT(' ');
    }
done:
    if (cap) buf[n < cap ? n : cap - 1] = 0;
    return (int)n;
#undef PUT
}

int ksnprintf(char *buf, unsigned cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return r;
}
