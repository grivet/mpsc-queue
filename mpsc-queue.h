/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Gaëtan Rivet
 */

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
};

/* Producer API. */

static inline
void mpsc_queue_insert(struct mpsc_queue *queue, struct mpsc_queue_node *node);

/* Insert a list of nodes in a single operation.
 * The nodes must all be appropriately linked from
 * first to last. */
static inline
void mpsc_queue_insert_list(struct mpsc_queue *queue,
                            struct mpsc_queue_node *first,
                            struct mpsc_queue_node *last);

/* Insert a number of nodes at once.
 * The nodes will be linked together before
 * being inserted in the queue. */
static inline
void mpsc_queue_insert_batch(struct mpsc_queue *queue,
                             size_t n_nodes,
                             struct mpsc_queue_node *node_ptrs[n_nodes]);

/* Consumer API. */

#define MPSC_QUEUE_FOR_EACH(node, queue) \
    for (node = mpsc_queue_tail(queue); node != NULL; \
         node = mpsc_queue_next((queue), node))

#define MPSC_QUEUE_FOR_EACH_POP(node, queue) \
    while ((node = mpsc_queue_pop(queue)))

enum mpsc_queue_poll_result {
    MPSC_QUEUE_EMPTY,
    MPSC_QUEUE_ITEM,
    MPSC_QUEUE_RETRY,
};

static inline
void mpsc_queue_init(struct mpsc_queue *queue);

/* Insert at the front of the queue. Only the consumer can do it. */
static inline
void mpsc_queue_push_front(struct mpsc_queue *queue, struct mpsc_queue_node *node);

static inline
enum mpsc_queue_poll_result
mpsc_queue_poll(struct mpsc_queue *queue, struct mpsc_queue_node **node);

static inline
struct mpsc_queue_node *
mpsc_queue_pop(struct mpsc_queue *queue);

static inline
struct mpsc_queue_node *
mpsc_queue_tail(struct mpsc_queue *queue);

static inline
struct mpsc_queue_node *
mpsc_queue_next(struct mpsc_queue *queue,
                struct mpsc_queue_node *prev);

/*******************/
/* Implementation. */
/*******************/

/* Producer API. */

static inline void
mpsc_queue_insert(struct mpsc_queue *queue, struct mpsc_queue_node *node)
{
    mpsc_queue_insert_list(queue, node, node);
}

static inline
void mpsc_queue_insert_list(struct mpsc_queue *queue,
                            struct mpsc_queue_node *first,
                            struct mpsc_queue_node *last)
{
    struct mpsc_queue_node *prev;

    atomic_store_explicit(&last->next, NULL, memory_order_relaxed);
    prev = atomic_exchange_explicit(&queue->head, last, memory_order_acq_rel);
    atomic_store_explicit(&prev->next, first, memory_order_release);
}

static inline
void mpsc_queue_insert_batch(struct mpsc_queue *queue,
                             size_t n_nodes,
                             struct mpsc_queue_node *node_ptrs[n_nodes])
{
    struct mpsc_queue_node *first, *last, *node;

    if (n_nodes == 0) {
        return;
    }

    first = node_ptrs[0];
    last = node_ptrs[n_nodes - 1];

    for (size_t i = 0; i < n_nodes - 1; i++) {
        node = node_ptrs[i];
        atomic_store_explicit(&node->next, node_ptrs[i + 1],
                              memory_order_relaxed);
    }
    mpsc_queue_insert_list(queue, first, last);
}

/* Consumer API. */

static inline void
mpsc_queue_init(struct mpsc_queue *queue)
{
    atomic_store_explicit(&queue->head, &queue->stub, memory_order_relaxed);
    atomic_store_explicit(&queue->tail, &queue->stub, memory_order_relaxed);
    atomic_store_explicit(&queue->stub.next, NULL, memory_order_relaxed);
}

static inline
void mpsc_queue_push_front(struct mpsc_queue *queue, struct mpsc_queue_node *node)
{
    struct mpsc_queue_node *tail;

    tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    atomic_store_explicit(&node->next, tail, memory_order_relaxed);
    atomic_store_explicit(&queue->tail, node, memory_order_relaxed);
}

static inline bool
mpsc_queue_is_empty(struct mpsc_queue *queue)
{
    struct mpsc_queue_node *tail;
    struct mpsc_queue_node *next;
    struct mpsc_queue_node *head;

    tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    next = atomic_load_explicit(&tail->next, memory_order_acquire);
    head = atomic_load_explicit(&queue->head, memory_order_acquire);

    return (tail == &queue->stub &&
            next == NULL &&
            tail == head);
}

static inline enum mpsc_queue_poll_result
mpsc_queue_poll(struct mpsc_queue *queue, struct mpsc_queue_node **node)
{
    struct mpsc_queue_node *tail;
    struct mpsc_queue_node *next;
    struct mpsc_queue_node *head;

    tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    next = atomic_load_explicit(&tail->next, memory_order_acquire);

    if (tail == &queue->stub) {
        if (next == NULL) {
            head = atomic_load_explicit(&queue->head, memory_order_acquire);
            if (tail != head) {
                return MPSC_QUEUE_RETRY;
            } else {
                return MPSC_QUEUE_EMPTY;
            }
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

    return MPSC_QUEUE_RETRY;
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

static inline struct mpsc_queue_node *
mpsc_queue_tail(struct mpsc_queue *queue)
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

static inline struct mpsc_queue_node *
mpsc_queue_next(struct mpsc_queue *queue,
                struct mpsc_queue_node *prev)
{
    struct mpsc_queue_node *next;

    next = atomic_load_explicit(&prev->next, memory_order_acquire);
    if (next == &queue->stub) {
        next = atomic_load_explicit(&next->next, memory_order_acquire);
    }
    return next;
}

#endif /* MPSC_QUEUE_H */
