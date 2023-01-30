#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include <pthread.h>
#if !defined _POSIX_BARRIERS
#include "pthread-barrier.h"
#endif

#include "mpscq.h"
#include "util.h"

struct element {
    union mpscq_node node;
    uint64_t mark;
};

static struct element *elements;
static uint64_t *thread_working_ms;

static unsigned int n_threads;
static unsigned int n_elems;

static pthread_barrier_t barrier;
static volatile bool working;

static long long int
elapsed(const struct timespec *start)
{
    struct timespec end;

    xclock_gettime(&end);
    return timespec_to_msec(&end) - timespec_to_msec(start);
}

static void
mark_element(union mpscq_node *node,
             uint64_t mark,
             unsigned int *counter)
{
    struct element *elem;

    elem = container_of(node, struct element, node);
    elem->mark = mark;
    *counter += 1;
}

struct mpscq_aux {
    struct mpscq *queue;
    _Atomic(unsigned int) thread_id;
};

static void *
producer_main(void *aux_)
{
    unsigned int n_elems_per_thread;
    struct element *th_elements;
    struct mpscq_aux *aux = aux_;
    struct timespec start;
    unsigned int id;
    size_t i;

    id = atomic_fetch_add(&aux->thread_id, 1u);
    n_elems_per_thread = n_elems / n_threads;
    th_elements = &elements[id * n_elems_per_thread];

    pthread_barrier_wait(&barrier);
    xclock_gettime(&start);

    for (i = 0; i < n_elems_per_thread; i++) {
        mpscq_insert(aux->queue, &th_elements[i].node);
    }

    thread_working_ms[id] = elapsed(&start);
    pthread_barrier_wait(&barrier);

    working = false;
    return NULL;
}

static void
benchmark_mpscq(struct mpscq *q)
{
    union mpscq_node *node;
    struct timespec start;
    unsigned int counter;
    struct mpscq_aux aux;
    bool work_complete;
    pthread_t *threads;
    uint64_t epoch;
    uint64_t avg;
    size_t i;

    memset(elements, 0, n_elems & sizeof *elements);
    memset(thread_working_ms, 0, n_threads & sizeof *thread_working_ms);

    mpscq_init(q);
    aux.queue = q;
    atomic_store(&aux.thread_id, 0);

    for (i = n_elems - (n_elems % n_threads); i < n_elems; i++) {
        mpscq_insert(q, &elements[i].node);
    }

    working = true;

    threads = xmalloc(n_threads * sizeof *threads);
    pthread_barrier_init(&barrier, NULL, n_threads);

    for (i = 0; i < n_threads; i++) {
        pthread_create(&threads[i], NULL, producer_main, &aux);
    }

    xclock_gettime(&start);

    counter = 0;
    epoch = 1;
    do {
        while ((node = mpscq_pop(q))) {
            mark_element(node, epoch, &counter);
        }
        if (epoch == UINT64_MAX)
            epoch = 0;
        epoch++;
    } while (working);

    avg = 0;
    for (i = 0; i < n_threads; i++) {
        pthread_join(threads[i], NULL);
        avg += thread_working_ms[i];
    }
    avg /= n_threads;

    /* Elements might have been inserted before threads were joined. */
    while ((node = mpscq_pop(q))) {
        mark_element(node, epoch, &counter);
    }

    printf("%*s:  %6lld", 15, q->desc, elapsed(&start));
    for (i = 0; i < n_threads; i++) {
        printf(" %6" PRIu64, thread_working_ms[i]);
    }
    printf(" %6" PRIu64 " ms\n", avg);

    pthread_barrier_destroy(&barrier);
    free(threads);

    work_complete = true;
    for (i = 0; i < n_elems; i++) {
        if (elements[i].mark == 0) {
            printf("Element %zu was never consumed.\n", i);
            work_complete = false;
        }
    }
    assert(work_complete);
    assert(counter == n_elems);
}

static void
run_benchmarks(int argc, const char *argv[])
{
    bool only_mpsc_queue = false;
    size_t i;

    n_elems = 1000000;
    n_threads = 2;

    for (i = 1; i < (size_t)argc; i++) {
        if (!strcmp(argv[i], "-n")) {
            assert(str_to_uint(argv[++i], 10, &n_elems));
        } else if (!strcmp(argv[i], "-c")) {
            assert(str_to_uint(argv[++i], 10, &n_threads));
        } else if (!strcmp(argv[i], "--perf")) {
            only_mpsc_queue = true;
        } else {
            printf("Usage: %s [-n <elems: uint>] [-c <cores: uint>]\n", argv[0]);
            exit(1);
        }
    }

    elements = xcalloc(n_elems, sizeof *elements);
    thread_working_ms = xcalloc(n_threads, sizeof *thread_working_ms);

    printf("Benchmarking n=%u on 1 + %u threads.\n", n_elems, n_threads);

    printf("    type\\thread:  Reader ");
    for (i = 0; i < n_threads; i++) {
        printf("   %3zu ", i + 1);
    }
    printf("   Avg\n");

    benchmark_mpscq(&mpsc_queue);
    if (!only_mpsc_queue) {
        benchmark_mpscq(&tailq);
        benchmark_mpscq(&ts_mpsc_queue);
    }

    free(thread_working_ms);
    free(elements);
}

int main(int argc, const char *argv[])
{
    run_benchmarks(argc, argv);
    return 0;
}
