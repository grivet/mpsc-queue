#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include <pthread.h>

#include <sys/queue.h>

#include "mpsc-queue.h"

#include "ts-mpsc-queue.h"
#include "util.h"

struct element {
    union {
        TAILQ_ENTRY(element) tailq;
        struct ts_mpsc_queue_node ts;
        struct mpsc_queue_node dv;
    } node;
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
test_tailq_mark_element(struct element *elem,
                        uint64_t mark,
                        unsigned int *counter)
{
    elem->mark = mark;
    *counter += 1;
}

TAILQ_HEAD(element_tailq, element);

struct tailq_aux {
    struct element_tailq *queue;
    pthread_spinlock_t lock;
    _Atomic(unsigned int) thread_id;
};

#define tailq_lock_init(l) do { pthread_spin_init(l, PTHREAD_PROCESS_PRIVATE); } while (0)
#define tailq_lock_destroy(l) do { pthread_spin_destroy(l); } while (0)
#define tailq_lock(l) do { pthread_spin_lock(l); } while (0)
#define tailq_unlock(l) do { pthread_spin_unlock(l); } while (0)

static void *
tailq_insert_thread(void *aux_)
{
    unsigned int n_elems_per_thread;
    struct element *th_elements;
    struct tailq_aux *aux = aux_;
    struct timespec start;
    unsigned int id;
    size_t i;

    id = atomic_fetch_add(&aux->thread_id, 1u);
    n_elems_per_thread = n_elems / n_threads;
    th_elements = &elements[id * n_elems_per_thread];

    pthread_barrier_wait(&barrier);
    xclock_gettime(&start);

    for (i = 0; i < n_elems_per_thread; i++) {
        tailq_lock(&aux->lock);
        TAILQ_INSERT_TAIL(aux->queue, &th_elements[i], node.tailq);
        tailq_unlock(&aux->lock);
    }

    thread_working_ms[id] = elapsed(&start);
    pthread_barrier_wait(&barrier);

    working = false;

    return NULL;
}

static void
benchmark_tailq(void)
{
    struct element_tailq tailq =
        TAILQ_HEAD_INITIALIZER(tailq);
    struct timespec start;
    struct tailq_aux aux;
    unsigned int counter;
    bool work_complete;
    pthread_t *threads;
    uint64_t epoch;
    uint64_t avg;
    size_t i;

    memset(elements, 0, n_elems & sizeof *elements);
    memset(thread_working_ms, 0, n_threads & sizeof *thread_working_ms);

    aux.queue = &tailq;
    atomic_store(&aux.thread_id, 0);
    tailq_lock_init(&aux.lock);

    for (i = n_elems - (n_elems % n_threads); i < n_elems; i++) {
        TAILQ_INSERT_TAIL(&tailq, &elements[i], node.tailq);
    }

    working = true;

    threads = xmalloc(n_threads * sizeof *threads);
    pthread_barrier_init(&barrier, NULL, n_threads);

    for (i = 0; i < n_threads; i++) {
        pthread_create(&threads[i], NULL, tailq_insert_thread, &aux);
    }

    xclock_gettime(&start);

    counter = 0;
    epoch = 1;
    do {
        struct element *elem;

        tailq_lock(&aux.lock);
        if (!TAILQ_EMPTY(&tailq)) {
            elem = TAILQ_FIRST(&tailq);
            TAILQ_REMOVE(&tailq, elem, node.tailq);
            test_tailq_mark_element(elem, epoch, &counter);
        }
        tailq_unlock(&aux.lock);
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
    while (!TAILQ_EMPTY(&tailq)) {
        struct element *elem = TAILQ_FIRST(&tailq);
        TAILQ_REMOVE(&tailq, elem, node.tailq);
        test_tailq_mark_element(elem, epoch, &counter);
    }

    printf("          tailq pop:  %6lld", elapsed(&start));
    for (i = 0; i < n_threads; i++) {
        printf(" %6" PRIu64, thread_working_ms[i]);
    }
    printf(" %6" PRIu64 " ms\n", avg);

    tailq_lock_destroy(&aux.lock);
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
test_ts_mpsc_queue_mark_element(struct ts_mpsc_queue_node *node,
                           uint64_t mark,
                           unsigned int *counter)
{
    struct element *elem;

    elem = container_of(node, struct element, node.ts);
    elem->mark = mark;
    *counter += 1;
}

struct ts_mpscq_aux {
    struct ts_mpsc_queue *queue;
    _Atomic(unsigned int) thread_id;
};

static void *
ts_mpsc_queue_insert_thread(void *aux_)
{
    unsigned int n_elems_per_thread;
    struct element *th_elements;
    struct ts_mpscq_aux *aux = aux_;
    struct timespec start;
    unsigned int id;
    size_t i;

    id = atomic_fetch_add(&aux->thread_id, 1u);
    n_elems_per_thread = n_elems / n_threads;
    th_elements = &elements[id * n_elems_per_thread];

    pthread_barrier_wait(&barrier);
    xclock_gettime(&start);

    for (i = 0; i < n_elems_per_thread; i++) {
        ts_mpsc_queue_insert(aux->queue, &th_elements[i].node.ts);
    }

    thread_working_ms[id] = elapsed(&start);
    pthread_barrier_wait(&barrier);

    working = false;

    return NULL;
}

static void
benchmark_ts_mpsc_queue_pop(void)
{
    struct ts_mpsc_queue_node *node;
    struct ts_mpsc_queue queue;
    struct timespec start;
    unsigned int counter;
    bool work_complete;
    pthread_t *threads;
    struct ts_mpscq_aux aux;
    uint64_t epoch;
    uint64_t avg;
    size_t i;

    memset(elements, 0, n_elems & sizeof *elements);
    memset(thread_working_ms, 0, n_threads & sizeof *thread_working_ms);

    ts_mpsc_queue_init(&queue);

    aux.queue = &queue;
    atomic_store(&aux.thread_id, 0);

    for (i = n_elems - (n_elems % n_threads); i < n_elems; i++) {
        ts_mpsc_queue_insert(&queue, &elements[i].node.ts);
    }

    working = true;

    threads = xmalloc(n_threads * sizeof *threads);
    pthread_barrier_init(&barrier, NULL, n_threads);

    for (i = 0; i < n_threads; i++) {
        pthread_create(&threads[i], NULL, ts_mpsc_queue_insert_thread, &aux);
    }

    xclock_gettime(&start);

    counter = 0;
    epoch = 1;
    do {
        while ((node = ts_mpsc_queue_pop(&queue))) {
            test_ts_mpsc_queue_mark_element(node, epoch, &counter);
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
    while ((node = ts_mpsc_queue_pop(&queue))) {
        test_ts_mpsc_queue_mark_element(node, epoch, &counter);
    }

    printf("  treiber stack pop:  %6lld", elapsed(&start));
    for (i = 0; i < n_threads; i++) {
        printf(" %6" PRIu64, thread_working_ms[i]);
    }
    printf(" %6" PRIu64 " ms\n", avg);

    ts_mpsc_queue_destroy(&queue);
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
test_mpsc_queue_mark_element(struct mpsc_queue_node *node,
                           uint64_t mark,
                           unsigned int *counter)
{
    struct element *elem;

    elem = container_of(node, struct element, node.dv);
    elem->mark = mark;
    *counter += 1;
}

struct mpscq_aux {
    struct mpsc_queue *queue;
    _Atomic(unsigned int) thread_id;
};

static void *
mpsc_queue_insert_thread(void *aux_)
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
        mpsc_queue_insert(aux->queue, &th_elements[i].node.dv);
    }

    thread_working_ms[id] = elapsed(&start);
    pthread_barrier_wait(&barrier);

    working = false;

    return NULL;
}

static void
benchmark_mpsc_queue_pop(void)
{
    enum mpsc_queue_poll_result poll_result;
    struct mpsc_queue_node *node;
    struct mpsc_queue queue;
    struct timespec start;
    unsigned int counter;
    bool work_complete;
    pthread_t *threads;
    struct mpscq_aux aux;
    uint64_t epoch;
    uint64_t avg;
    size_t i;

    memset(elements, 0, n_elems & sizeof *elements);
    memset(thread_working_ms, 0, n_threads & sizeof *thread_working_ms);

    mpsc_queue_init(&queue);

    if (!mpsc_queue_try_lock(&queue)) {
        abort();
    }

    aux.queue = &queue;
    atomic_store(&aux.thread_id, 0);

    for (i = n_elems - (n_elems % n_threads); i < n_elems; i++) {
        mpsc_queue_insert(&queue, &elements[i].node.dv);
    }

    working = true;

    threads = xmalloc(n_threads * sizeof *threads);
    pthread_barrier_init(&barrier, NULL, n_threads);

    for (i = 0; i < n_threads; i++) {
        pthread_create(&threads[i], NULL, mpsc_queue_insert_thread, &aux);
    }

    xclock_gettime(&start);

    counter = 0;
    epoch = 1;
    do {
        while ((poll_result = mpsc_queue_poll(&queue, &node))) {
            if (poll_result == MPSC_QUEUE_RETRY) {
                continue;
            }
            test_mpsc_queue_mark_element(node, epoch, &counter);
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
    while ((poll_result = mpsc_queue_poll(&queue, &node))) {
        if (poll_result == MPSC_QUEUE_RETRY) {
            continue;
        }
        test_mpsc_queue_mark_element(node, epoch, &counter);
    }

    printf("     mpsc-queue pop:  %6lld", elapsed(&start));
    for (i = 0; i < n_threads; i++) {
        printf(" %6" PRIu64, thread_working_ms[i]);
    }
    printf(" %6" PRIu64 " ms\n", avg);

    mpsc_queue_unlock(&queue);
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

    printf("        type\\thread:  Reader ");
    for (i = 0; i < n_threads; i++) {
        printf("   %3zu ", i + 1);
    }
    printf("   Avg\n");

    if (!only_mpsc_queue) {
        benchmark_tailq();
        benchmark_ts_mpsc_queue_pop();
    }
    benchmark_mpsc_queue_pop();

    free(thread_working_ms);
    free(elements);
}

int main(int argc, const char *argv[])
{
    run_benchmarks(argc, argv);
    return 0;
}
