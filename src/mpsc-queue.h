#ifndef MPSC_QUEUE_H
#define MPSC_QUEUE_H

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

void mpsc_queue_insert(struct mpsc_queue *queue, struct mpsc_queue_node *node);

/* Consumer API. */

enum mpsc_queue_poll_result {
    MPSC_QUEUE_EMPTY,
    MPSC_QUEUE_ITEM,
    MPSC_QUEUE_RETRY,
};

void mpsc_queue_init(struct mpsc_queue *queue);
void mpsc_queue_destroy(struct mpsc_queue *queue);

enum mpsc_queue_poll_result
mpsc_queue_poll(struct mpsc_queue *queue, struct mpsc_queue_node **node);

#endif /* MPSC_QUEUE_H */
