#include <stdlib.h>
#include <stdatomic.h>

#include "mpsc-queue.h"

/* Consumer API. */

void
mpsc_queue_init(struct mpsc_queue *queue)
{
    atomic_store_explicit(&queue->head, &queue->stub, memory_order_relaxed);
    atomic_store_explicit(&queue->tail, &queue->stub, memory_order_relaxed);
    atomic_store_explicit(&queue->stub.next, NULL, memory_order_relaxed);
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

/* Producer API. */

void
mpsc_queue_insert(struct mpsc_queue *queue, struct mpsc_queue_node *node)
{
    struct mpsc_queue_node *prev;

    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
    prev = atomic_exchange_explicit(&queue->head, node, memory_order_acq_rel);
    atomic_store_explicit(&prev->next, node, memory_order_release);
}
