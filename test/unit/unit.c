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

static void
test_mpsc_queue_poll(void)
{
    struct mpsc_queue *q = mq_create();
    struct mpsc_queue_node *node;
    struct element elements[1];
    struct mpsc_queue_node *prevs[ARRAY_SIZE(elements)];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        elements[i].id = i;
    }

    prevs[0] = mpsc_queue_insert_begin(q, &elements[0].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_RETRY);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_RETRY);

    mpsc_queue_insert_end(prevs[0], &elements[0].node);
    assert(mpsc_queue_poll(q, &node) == MPSC_QUEUE_ITEM);
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
