#ifndef UTIL_H
#define UTIL_H

#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#ifndef offsetof
#define offsetof(type, field) \
    ((size_t)((char *)&(((type *)0)->field) - (char *)0))
#endif

#define container_of(addr, type, field) \
    ((type *) (void *) ((char *) (addr) - offsetof (type, field)))

#define ARRAY_SIZE(ar) (sizeof(ar) / sizeof(ar[0]))

#define MIN(a, b) (a < b ? a : b)

void out_of_memory(void);
void *xcalloc(size_t count, size_t size);
void *xzalloc(size_t size);
void *xmalloc(size_t size);

void xclock_gettime(struct timespec *ts);

long long int timespec_to_msec(const struct timespec *ts);
long long int timespec_to_usec(const struct timespec *ts);

bool str_to_uint(const char *s, int base, unsigned int *result);

#endif /* UTIL_H */
