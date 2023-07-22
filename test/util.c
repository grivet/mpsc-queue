#define _XOPEN_SOURCE 700
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "util.h"

uint32_t rand_seed;

void
random_init(uint32_t seed)
{
    rand_seed = seed;
}

void
xabort(const char *msg)
{
    fprintf(stderr, "%s.\n", msg);
    abort();
}

void
out_of_memory(void)
{
    xabort("virtual memory exhausted");
}

void *
xcalloc(size_t count, size_t size)
{
    void *p = count && size ? calloc(count, size) : malloc(1);
    if (p == NULL) {
        out_of_memory();
    }
    return p;
}

void *
xzalloc(size_t size)
{
    return xcalloc(1, size);
}

void *
xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (p == NULL) {
        out_of_memory();
    }
    return p;
}

void
xclock_gettime(struct timespec *ts)
{
    if (clock_gettime(CLOCK_MONOTONIC, ts) == -1) {
        xabort("clock_gettime failed");
    }
}

long long int
timespec_to_msec(const struct timespec *ts)
{
    return (long long int) ts->tv_sec * 1000 + ts->tv_nsec / (1000 * 1000);
}

long long int
timespec_to_usec(const struct timespec *ts)
{
    return (long long int) ts->tv_sec * 1000 * 1000 + ts->tv_nsec / 1000;
}

long long int
time_usec(void)
{
    struct timespec ts;

    xclock_gettime(&ts);
    return timespec_to_usec(&ts);
}

bool
str_to_uint(const char *s, int base, unsigned int *result)
{
    int save_errno = errno;
    long long ll;
    char *tail;

    ll = strtoll(s, &tail, base);
    if (errno == EINVAL || errno == ERANGE || tail == s) {
        errno = save_errno;
        *result = 0;
        return false;
    } else if (ll < 0 || ll > UINT_MAX) {
        *result = 0;
        return false;
    } else {
        errno = save_errno;
        *result = ll;
        return true;
    }
}
