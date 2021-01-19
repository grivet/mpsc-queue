#include "ts-mpsc-queue.h"

/* Consumer API. */

void ts_mpsc_queue_init(struct ts_mpsc_queue *queue)
{
    queue->head = NULL;
    queue->list = NULL;
    queue->tail = NULL;
}

void ts_mpsc_queue_destroy(struct ts_mpsc_queue *queue)
{
    /* nil */
    (void)queue;
}

struct node_pair {
    struct ts_mpsc_queue_node *head;
    struct ts_mpsc_queue_node *tail;
};

/* Empty the queue and return the nodes as an iterable list. */
static struct node_pair
ts_mpsc_queue_flush__(struct ts_mpsc_queue *queue)
{
    struct ts_mpsc_queue_node *stack;
    struct ts_mpsc_queue_node *head;
    struct ts_mpsc_queue_node *tail;

    stack = atomic_exchange_explicit(&queue->head, NULL, memory_order_acquire);

    head = queue->list;
    tail = queue->tail;

    if (stack != NULL) {
        /* Reverse the stack of elements. */
        struct ts_mpsc_queue_node *node;
        struct ts_mpsc_queue_node *prev;
        struct ts_mpsc_queue_node *next;

        prev = NULL;
        TS_MPSC_QUEUE_FOR_EACH_SAFE(node, stack, next) {
            node->next = prev;
            prev = node;
            if (next == NULL) {
                if (queue->tail != NULL) {
                    /* Enqueue reversed stack to current list */
                    queue->tail->next = node;
                } else {
                    /* Return reversed stack directly. */
                    head = node;
                }
                tail = stack;
            }
        }
    }

    /* Queue is flushed. */
    queue->list = NULL;
    queue->tail = NULL;

    return (struct node_pair){ head, tail };
}

/* Empty the queue and return the nodes as an iterable list. */
struct ts_mpsc_queue_node *
ts_mpsc_queue_flush(struct ts_mpsc_queue *queue)
{
    struct node_pair pair = ts_mpsc_queue_flush__(queue);

    return pair.head;
}

/* Remove one node from the queue and returns it, standalone. */
struct ts_mpsc_queue_node *
ts_mpsc_queue_pop(struct ts_mpsc_queue *queue)
{
    struct ts_mpsc_queue_node *node;

    if (queue->list == NULL) {
        struct node_pair pair = ts_mpsc_queue_flush__(queue);

        queue->list = pair.head;
        queue->tail = pair.tail;
    }

    if (queue->list == NULL) {
        return NULL;
    }

    node = queue->list;
    queue->list = node->next;
    node->next = NULL;

    if (node == queue->tail) {
        queue->tail = NULL;
    }

    return node;
}

bool
ts_mpsc_queue_is_empty(struct ts_mpsc_queue *queue)
{
    struct ts_mpsc_queue_node *head;

    if (queue->list != NULL)
        return false;

    head = atomic_load_explicit(&queue->head, memory_order_acquire);
    if (head != NULL)
        return false;

    return true;
}

/* Producer API. */

void
ts_mpsc_queue_insert(struct ts_mpsc_queue *queue, struct ts_mpsc_queue_node *node)
{
    struct ts_mpsc_queue_node *next;

    next = atomic_load_explicit(&queue->head, memory_order_acquire);
    do {
        node->next = next;
    } while (!atomic_compare_exchange_weak_explicit(&queue->head, &next, node,
            memory_order_release, memory_order_relaxed));
}
