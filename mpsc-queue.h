#ifndef MPSC_QUEUE_H
#define MPSC_QUEUE_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>

struct mpsc_queue_node {
    _Atomic(struct mpsc_queue_node *) next;
};

struct mpsc_queue {
    _Atomic(struct mpsc_queue_node *) head;
    _Atomic(struct mpsc_queue_node *) tail;
    struct mpsc_queue_node stub;
    atomic_flag read_locked;
};

/* Consumer API. */

/* Return true if queue is acquired for reading. */
static inline
bool mpsc_queue_acquire(struct mpsc_queue *queue);
/* Unlock the queue. */
static inline
void mpsc_queue_release(struct mpsc_queue *queue);

enum mpsc_queue_poll_result {
    MPSC_QUEUE_EMPTY,
    MPSC_QUEUE_ITEM,
    MPSC_QUEUE_RETRY,
};

static inline
void mpsc_queue_init(struct mpsc_queue *queue);

static inline
enum mpsc_queue_poll_result
mpsc_queue_poll(struct mpsc_queue *queue, struct mpsc_queue_node **node);

static inline
struct mpsc_queue_node *
mpsc_queue_pop(struct mpsc_queue *queue);

#define MPSC_QUEUE_FOR_EACH(node, queue) \
    for (node = _mpsc_queue_tail(queue); node != NULL; \
         node = atomic_load_explicit(&node->next, memory_order_acquire))

#define MPSC_QUEUE_FOR_EACH_POP(node, queue) \
    while ((node = mpsc_queue_pop(queue)))

/* Producer API. */

static inline
void mpsc_queue_insert(struct mpsc_queue *queue, struct mpsc_queue_node *node);

/* Implementation. */

/* Consumer API. */

/* Return true if queue is acquired for reading. */
static inline bool
mpsc_queue_acquire(struct mpsc_queue *queue)
{
    return atomic_flag_test_and_set(&queue->read_locked) == false;
}

/* Unlock the queue. */
static inline void
mpsc_queue_release(struct mpsc_queue *queue)
{
    atomic_flag_clear(&queue->read_locked);
}

void
mpsc_queue_init(struct mpsc_queue *queue)
{
    atomic_store_explicit(&queue->head, &queue->stub, memory_order_relaxed);
    atomic_store_explicit(&queue->tail, &queue->stub, memory_order_relaxed);
    atomic_store_explicit(&queue->stub.next, NULL, memory_order_relaxed);
    atomic_flag_clear(&queue->read_locked);
}

enum mpsc_queue_poll_result
mpsc_queue_poll(struct mpsc_queue *queue, struct mpsc_queue_node **node)
{
    struct mpsc_queue_node *tail;
    struct mpsc_queue_node *next;
    struct mpsc_queue_node *head;

    tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    next = atomic_load_explicit(&tail->next, memory_order_acquire);

    if (tail == &queue->stub) {
        if (next == NULL) {
            return MPSC_QUEUE_EMPTY;
        }

        atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
        tail = next;
        next = atomic_load_explicit(&tail->next, memory_order_acquire);
    }

    if (next != NULL) {
        atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
        *node = tail;
        return MPSC_QUEUE_ITEM;
    }

    head = atomic_load_explicit(&queue->head, memory_order_acquire);
    if (tail != head) {
        return MPSC_QUEUE_RETRY;
    }

    mpsc_queue_insert(queue, &queue->stub);

    next = atomic_load_explicit(&tail->next, memory_order_acquire);
    if (next != NULL) {
        atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
        *node = tail;
        return MPSC_QUEUE_ITEM;
    }

    return MPSC_QUEUE_EMPTY;
}

static inline
struct mpsc_queue_node *
mpsc_queue_pop(struct mpsc_queue *queue)
{
    enum mpsc_queue_poll_result result;
    struct mpsc_queue_node *node;

    do {
        result = mpsc_queue_poll(queue, &node);
        if (result == MPSC_QUEUE_EMPTY) {
            return NULL;
        }
    } while (result == MPSC_QUEUE_RETRY);

    return node;
}

/* Producer API. */

static inline void
mpsc_queue_insert(struct mpsc_queue *queue, struct mpsc_queue_node *node)
{
    struct mpsc_queue_node *prev;

    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
    prev = atomic_exchange_explicit(&queue->head, node, memory_order_acq_rel);
    atomic_store_explicit(&prev->next, node, memory_order_release);
}

/* helpers. */

static inline struct mpsc_queue_node *
_mpsc_queue_tail(struct mpsc_queue *queue)
{
    struct mpsc_queue_node *tail;
    struct mpsc_queue_node *next;

    tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    next = atomic_load_explicit(&tail->next, memory_order_acquire);

    if (tail == &queue->stub) {
        if (next == NULL) {
            return NULL;
        }

        atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
        tail = next;
    }

    return tail;
}

#endif /* MPSC_QUEUE_H */
