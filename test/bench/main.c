#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include <pthread.h>
#if __APPLE__
#include "pthread-barrier.h"
#endif

#include "mpscq.h"
#include "util.h"

#define MAX_BATCH_SIZE 64
#define DEFAULT_BATCH_SIZE 64

struct element {
    union mpscq_node node;
    uint64_t mark;
};

static bool print_csv;

static struct element *elements;
static uint64_t *thread_working_ms;

static unsigned int batch_size;
static unsigned int n_threads;
static unsigned int n_elems;
static bool warming;

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
print_header(void)
{
    if (!print_csv) {
        printf("Benchmarking n=%u,batch=%u on 1 + %u threads.\n",
                n_elems, batch_size, n_threads);
        printf("    type\\thread:  Reader ");
        for (unsigned int i = 0; i < n_threads; i++) {
            printf("   %3u ", i + 1);
        }
        printf("   Avg\n");
    }
}

static void
print_test_result(struct mpscq *q, long long int consumer_time)
{
    unsigned int i;
    uint64_t avg;

    avg = 0;
    for (i = 0; i < n_threads; i++) {
        avg += thread_working_ms[i];
    }
    avg /= n_threads;

    if (print_csv) {
        printf("%s-%u-consumer,%lld\n",
               q->desc, batch_size, consumer_time);
        printf("%s-%u-producers-avg,%" PRIu64 "\n",
               q->desc, batch_size, avg);
    } else {
        printf("%*s:  %6lld", 15, q->desc, consumer_time);
        for (i = 0; i < n_threads; i++) {
            printf(" %6" PRIu64, thread_working_ms[i]);
        }
        printf(" %6" PRIu64 " ms\n", avg);
    }
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
    union mpscq_node *batch[MAX_BATCH_SIZE];
    unsigned int n_elems_per_thread;
    struct element *th_elements;
    struct mpscq_aux *aux = aux_;
    struct timespec start;
    unsigned int n_batch;
    unsigned int id;
    size_t i, n;

    id = atomic_fetch_add(&aux->thread_id, 1u);

    while (true) {
        pthread_barrier_wait(&barrier);
        if (!working) {
            break;
        }
        n_elems_per_thread = n_elems / n_threads;
        n_batch = n_elems_per_thread / batch_size;
        th_elements = &elements[id * n_elems_per_thread];
        xclock_gettime(&start);

        n = 0;
        for (i = 0; i < n_batch; i++) {
            for (size_t j = 0; j < batch_size; j++) {
                batch[j] = &th_elements[n++].node;
            }
            mpscq_insert_batch(aux->queue, batch_size, batch);
        }
        while (n < n_elems_per_thread) {
            mpscq_insert(aux->queue, &th_elements[n++].node);
        }

        thread_working_ms[id] = elapsed(&start);
        pthread_barrier_wait(&barrier);
    }

    return NULL;
}

static void
benchmark_mpscq(struct mpscq *q, struct mpscq_aux *aux)
{
    long long int consumer_time;
    union mpscq_node *node;
    struct timespec start;
    unsigned int counter;
    uint64_t epoch;
    size_t i;

    memset(elements, 0, n_elems & sizeof *elements);
    memset(thread_working_ms, 0, n_threads & sizeof *thread_working_ms);

    mpscq_init(q);
    aux->queue = q;

    for (i = n_elems - (n_elems % n_threads); i < n_elems; i++) {
        mpscq_insert(q, &elements[i].node);
    }

    pthread_barrier_wait(&barrier);

    xclock_gettime(&start);
    counter = 0;
    epoch = 0;
    do {
        while ((node = mpscq_pop(q))) {
            mark_element(node, epoch, &counter);
        }
        epoch++;
    } while (counter != n_elems);

    consumer_time = elapsed(&start);
    pthread_barrier_wait(&barrier);

    if (warming) {
        return;
    }

    print_test_result(q, consumer_time);
}

static void
run_benchmarks(int argc, const char *argv[])
{
    bool with_treiber_stack = false;
    bool only_mpsc_queue = false;
    struct mpscq_aux aux;
    pthread_t *threads;
    size_t i;

    batch_size = DEFAULT_BATCH_SIZE;
    n_elems = 1000000;
    n_threads = 2;

    for (i = 1; i < (size_t)argc; i++) {
        if (!strcmp(argv[i], "-n")) {
            assert(str_to_uint(argv[++i], 10, &n_elems));
        } else if (!strcmp(argv[i], "-c")) {
            assert(str_to_uint(argv[++i], 10, &n_threads));
        } else if (!strcmp(argv[i], "--perf")) {
            only_mpsc_queue = true;
        } else if (!strcmp(argv[i], "--with-treiber-stack")) {
            with_treiber_stack = true;
        } else if (!strcmp(argv[i], "--csv")) {
            print_csv = true;
        } else if (!strcmp(argv[i], "-b")) {
            assert(str_to_uint(argv[++i], 10, &batch_size));
        } else {
            printf("Usage: %s [-n <elems: uint>] [-c <cores: uint>]\n", argv[0]);
            exit(1);
        }
    }

    if (batch_size > MAX_BATCH_SIZE) {
        fprintf(stderr, "Using maximum allowed batch size: %u",
                MAX_BATCH_SIZE);
        batch_size = MAX_BATCH_SIZE;
    }

    atomic_store(&aux.thread_id, 0);

    elements = xcalloc(n_elems, sizeof *elements);
    thread_working_ms = xcalloc(n_threads, sizeof *thread_working_ms);
    threads = xmalloc(n_threads * sizeof *threads);
    pthread_barrier_init(&barrier, NULL, n_threads + 1);
    working = true;

    for (i = 0; i < n_threads; i++) {
        pthread_create(&threads[i], NULL, producer_main, &aux);
    }

    {
        unsigned int n_elems_memo = n_elems;

        warming = true;
        n_elems = MIN(n_elems, 100000);
        benchmark_mpscq(&tailq, &aux);
        benchmark_mpscq(&tailq, &aux);
        benchmark_mpscq(&tailq, &aux);
        n_elems = n_elems_memo;
        warming = false;
    }

    print_header();

    benchmark_mpscq(&mpsc_queue, &aux);
    if (!only_mpsc_queue) {
        benchmark_mpscq(&tailq, &aux);
        if (with_treiber_stack) {
            benchmark_mpscq(&ts_mpsc_queue, &aux);
        }
    }
    working = false;
    pthread_barrier_wait(&barrier);

    for (i = 0; i < n_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(thread_working_ms);
    free(elements);
    free(threads);
}

int main(int argc, const char *argv[])
{
    run_benchmarks(argc, argv);
    return 0;
}
