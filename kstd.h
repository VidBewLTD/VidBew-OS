#ifndef KSTD_H
#define KSTD_H

/* Tiny freestanding "libc": just what the kernel actually uses.
 * memcpy/memset/memmove/memcmp keep their standard names because GCC may
 * emit calls to them on its own even in -ffreestanding builds. */

#include <stdarg.h>
#include "types.h"

void *memcpy(void *dst, const void *src, unsigned n);
void *memset(void *dst, int c, unsigned n);
void *memmove(void *dst, const void *src, unsigned n);
int   memcmp(const void *a, const void *b, unsigned n);

unsigned kstrlen(const char *s);
int  kstrcmp(const char *a, const char *b);
int  kstrncmp(const char *a, const char *b, unsigned n);
int  kstricmp(const char *a, const char *b);          /* case-insensitive */
void kstrlcpy(char *dst, const char *src, unsigned cap); /* always NUL-terminates */
void kstrlcat(char *dst, const char *src, unsigned cap);
int  katoi(const char *s);
char kupper(char c);
char klower(char c);

int ksnprintf(char *buf, unsigned cap, const char *fmt, ...);
int kvsnprintf(char *buf, unsigned cap, const char *fmt, va_list ap);

#endif
