#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include "mpsc-queue.h"
#include "util.h"

struct element {
    unsigned int id;
    struct mpsc_queue_node node;
};

static struct mpsc_queue *
mq_create(void)
{
    struct mpsc_queue *q = xmalloc(sizeof(*q));
    mpsc_queue_init(q);
    return q;
}

static void
mq_destroy(struct mpsc_queue *q)
{
    free(q);
}

static void
test_mpsc_queue_insert_ordered(void)
{
    struct mpsc_queue *q = mq_create();
    struct mpsc_queue_node *node;
    struct element elements[10];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        elements[i].id = i;
        mpsc_queue_insert(q, &elements[i].node);
    }

    i = 0;
    MPSC_QUEUE_FOR_EACH (node, q) {
        struct element *e = container_of(node, struct element, node);
        assert(e == &elements[i]);
        i++;
    }
    assert(i == ARRAY_SIZE(elements));

    MPSC_QUEUE_FOR_EACH_POP (node, q) {
        struct element *e = container_of(node, struct element, node);
        assert(e->id == (unsigned int)(e - elements));
    }

    mq_destroy(q);
}

static struct mpsc_queue_node *
mpsc_queue_insert_begin(struct mpsc_queue *queue, struct mpsc_queue_node *node)
{
    struct mpsc_queue_node *prev;

    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
    prev = atomic_exchange_explicit(&queue->head, node, memory_order_acq_rel);
    return prev;
}

static void
mpsc_queue_insert_end(struct mpsc_queue_node *prev, struct mpsc_queue_node *node)
{
    atomic_store_explicit(&prev->next, node, memory_order_release);
}

static void
test_mpsc_queue_insert_partial(void)
{
    struct mpsc_queue *q = mq_create();
    struct mpsc_queue_node *node;
    struct element elements[10];
    struct mpsc_queue_node *prevs[ARRAY_SIZE(elements)];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        elements[i].id = i;
        if (i > ARRAY_SIZE(elements) / 2) {
            prevs[i] = mpsc_queue_insert_begin(q, &elements[i].node);
        } else {
            prevs[i] = NULL;
            mpsc_queue_insert(q, &elements[i].node);
        }
    }

    /* Verify that when the chain is broken, iterators will stop. */
    i = 0;
    MPSC_QUEUE_FOR_EACH (node, q) {
        struct element *e = container_of(node, struct element, node);
        assert(e == &elements[i]);
        i++;
    }
    assert(i < ARRAY_SIZE(elements));

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        if (prevs[i] != NULL) {
            mpsc_queue_insert_end(prevs[i], &elements[i].node);
        }
    }

    i = 0;
    MPSC_QUEUE_FOR_EACH (node, q) {
        struct element *e = container_of(node, struct element, node);
        assert(e == &elements[i]);
        i++;
    }
    assert(i == ARRAY_SIZE(elements));

    MPSC_QUEUE_FOR_EACH_POP (node, q) {
        struct element *e = container_of(node, struct element, node);
        assert(e->id == (unsigned int)(e - elements));
    }

    mq_destroy(q);
}

struct mpsc_queue_poll_ctx {
    struct mpsc_queue *queue;
    struct mpsc_queue_node *tail;
    struct mpsc_queue_node *next;
    struct mpsc_queue_node *head;
    struct mpsc_queue_node **node;
    enum mpsc_queue_poll_result result;
    bool completed;
};
#define POLL_CTX(Q, TAIL, NEXT, HEAD, NODE) \
    (struct mpsc_queue_poll_ctx){ \
        .queue = Q, .tail = TAIL, .next = NEXT, .head = HEAD, .node = NODE, \
        .result = 0, .completed = false, \
    }
#define POLL_DONE(RESULT) \
    (struct mpsc_queue_poll_ctx){ \
        .queue = NULL, .tail = NULL, .next = NULL, .head = NULL, .node = NULL, \
        .result = RESULT, .completed = true \
    }

static inline struct mpsc_queue_poll_ctx
mpsc_queue_poll_begin(struct mpsc_queue *queue, struct mpsc_queue_node **node)
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
                return POLL_DONE(MPSC_QUEUE_RETRY);
            } else {
                return POLL_DONE(MPSC_QUEUE_EMPTY);
            }
        }

        atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
        tail = next;
        next = atomic_load_explicit(&tail->next, memory_order_acquire);
    }

    if (next != NULL) {
        atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
        *node = tail;
        return POLL_DONE(MPSC_QUEUE_ITEM);
    }

    head = atomic_load_explicit(&queue->head, memory_order_acquire);
    if (tail != head) {
        return POLL_DONE(MPSC_QUEUE_RETRY);
    }

    return POLL_CTX(queue, tail, next, head, node);
}

/* Call this function only *once* on a poll context. */
static inline enum mpsc_queue_poll_result
mpsc_queue_poll_end(struct mpsc_queue_poll_ctx ctx)
{
    struct mpsc_queue *queue = ctx.queue;
    struct mpsc_queue_node **node = ctx.node;
    struct mpsc_queue_node *tail = ctx.tail;
    struct mpsc_queue_node *next = ctx.next;

    mpsc_queue_insert(queue, &queue->stub);

    next = atomic_load_explicit(&tail->next, memory_order_acquire);
    if (next != NULL) {
        atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
        *node = tail;
        return MPSC_QUEUE_ITEM;
    }

    return MPSC_QUEUE_RETRY;
}

static void
test_mpsc_queue_poll(void)
{
    struct mpsc_queue *q = mq_create();
    struct mpsc_queue_node *node;
    struct element elements[3];
    struct mpsc_queue_node *prevs[ARRAY_SIZE(elements)];
    struct mpsc_queue_poll_ctx poll_ctx;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        elements[i].id = i;
    }

    /* Basic cases. */

    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_EMPTY);

    mpsc_queue_insert(q, &elements[0].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_EMPTY);

    mpsc_queue_insert(q, &elements[0].node);
    mpsc_queue_insert(q, &elements[1].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_EMPTY);

    /* Partial insertion cases. */

    /* If a single element is currently being inserted,
     * return value should be 'retry'. */

    prevs[0] = mpsc_queue_insert_begin(q, &elements[0].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_RETRY);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_RETRY);

    mpsc_queue_insert_end(prevs[0], &elements[0].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_EMPTY);

    /* Fully inserting n>1 nodes, then partially inserting
     * a last node, should return 'retry' after removing what
     * can be. */

    mpsc_queue_insert(q, &elements[0].node);
    mpsc_queue_insert(q, &elements[1].node);
    prevs[2] = mpsc_queue_insert_begin(q, &elements[2].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_RETRY);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_RETRY);

    mpsc_queue_insert_end(prevs[2], &elements[2].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_EMPTY);

    /* Partial insert + partial poll. */

    /* If a single element is in the queue,
     * then a poll is interrupted by a partial insert before
     * inserting the 'stub', completing the poll later
     * should result in a 'retry'. */

    mpsc_queue_insert(q, &elements[0].node);

    poll_ctx = mpsc_queue_poll_begin(q, &node);
    assert(poll_ctx.completed == false);

    prevs[1] = mpsc_queue_insert_begin(q, &elements[1].node);
    assert(mpsc_queue_poll_end(poll_ctx) == MPSC_QUEUE_RETRY);

    mpsc_queue_insert_end(prevs[1], &elements[1].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(node == &elements[0].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
    assert(node == &elements[1].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_EMPTY);

    mq_destroy(q);
}

static void
test_mpsc_queue_push_front(void)
{
    struct mpsc_queue *q = mq_create();
    struct mpsc_queue_node *node;
    struct element elements[10];
    size_t i;

    assert(mpsc_queue_pop(q) == NULL);
    mpsc_queue_push_front(q, &elements[0].node);
    node = mpsc_queue_pop(q);
    assert(node == &elements[0].node);
    assert(mpsc_queue_pop(q) == NULL);

    mpsc_queue_push_front(q, &elements[0].node);
    mpsc_queue_push_front(q, &elements[1].node);
    assert(mpsc_queue_pop(q) == &elements[1].node);
    assert(mpsc_queue_pop(q) == &elements[0].node);
    assert(mpsc_queue_pop(q) == NULL);

    mpsc_queue_push_front(q, &elements[1].node);
    mpsc_queue_push_front(q, &elements[0].node);
    mpsc_queue_insert(q, &elements[2].node);
    assert(mpsc_queue_pop(q) == &elements[0].node);
    assert(mpsc_queue_pop(q) == &elements[1].node);
    assert(mpsc_queue_pop(q) == &elements[2].node);
    assert(mpsc_queue_pop(q) == NULL);

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        elements[i].id = i;
        mpsc_queue_insert(q, &elements[i].node);
    }

    node = mpsc_queue_pop(q);
    mpsc_queue_push_front(q, node);
    assert(mpsc_queue_pop(q) == node);
    mpsc_queue_push_front(q, node);

    i = 0;
    MPSC_QUEUE_FOR_EACH (node, q) {
        struct element *e = container_of(node, struct element, node);
        assert(e == &elements[i]);
        i++;
    }
    assert(i == ARRAY_SIZE(elements));

    MPSC_QUEUE_FOR_EACH_POP (node, q) {
        struct element *e = container_of(node, struct element, node);
        assert(e->id == (unsigned int)(e - elements));
    }

    mq_destroy(q);
}

int main(void)
{
    test_mpsc_queue_insert_ordered();
    test_mpsc_queue_insert_partial();
    test_mpsc_queue_poll();
    test_mpsc_queue_push_front();
    return 0;
}
