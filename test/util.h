#ifndef UTIL_H
#define UTIL_H

#define _XOPEN_SOURCE 700

#include <stdint.h>
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

#define MIN(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (a) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (a) _b = (b); \
     _a > _b ? _a : _b; })

extern uint32_t rand_seed;

/* The state word must be initialized to non-zero */
static inline uint32_t
xorshift32(uint32_t x[1])
{
    /* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
    x[0] ^= x[0] << 13;
    x[0] ^= x[0] >> 17;
    x[0] ^= x[0] << 5;
    return x[0];
}

void random_init(uint32_t seed);

static inline uint32_t
random_u32(void)
{
    return xorshift32(&rand_seed);
}

static inline uint32_t
random_u32_range(uint32_t max)
{
    return random_u32() % max;
}

void out_of_memory(void);
void *xcalloc(size_t count, size_t size);
void *xzalloc(size_t size);
void *xmalloc(size_t size);

void xclock_gettime(struct timespec *ts);

long long int timespec_to_msec(const struct timespec *ts);
long long int timespec_to_usec(const struct timespec *ts);
long long int time_usec(void);

bool str_to_uint(const char *s, int base, unsigned int *result);

#endif /* UTIL_H */
