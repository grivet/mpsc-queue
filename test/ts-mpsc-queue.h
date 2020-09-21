#ifndef TS_MPSC_QUEUE_H
#define TS_MPSC_QUEUE_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>

struct ts_mpsc_queue_node {
    struct ts_mpsc_queue_node *next;
};

struct ts_mpsc_queue {
    _Atomic(struct ts_mpsc_queue_node *) head;
    struct ts_mpsc_queue_node *list;
    struct ts_mpsc_queue_node *tail;
};

/* Producer API. */

void ts_mpsc_queue_insert(struct ts_mpsc_queue *queue, struct ts_mpsc_queue_node *node);

/* Consumer API. */

#define TS_MPSC_QUEUE_FOR_EACH(ITER, LIST) \
    for ((ITER) = (LIST); (ITER) != NULL; (ITER) = (ITER)->next)

#define TS_MPSC_QUEUE_FOR_EACH_SAFE(ITER, LIST, NEXT) \
    for ((ITER) = (LIST), (NEXT) = (ITER) ? (ITER)->next : NULL; \
         (ITER) != NULL; \
         (ITER) = (NEXT), (NEXT) = (ITER) ? (ITER)->next : NULL)

void ts_mpsc_queue_init(struct ts_mpsc_queue *queue);
void ts_mpsc_queue_destroy(struct ts_mpsc_queue *queue);

/* Empty the queue and return the nodes as an iterable list. */
struct ts_mpsc_queue_node *ts_mpsc_queue_flush(struct ts_mpsc_queue *queue);
/* Remove one node from the queue and returns it, standalone. */
struct ts_mpsc_queue_node *ts_mpsc_queue_pop(struct ts_mpsc_queue *queue);

bool ts_mpsc_queue_is_empty(struct ts_mpsc_queue *queue);

#endif /* TS_MPSC_QUEUE_H */
