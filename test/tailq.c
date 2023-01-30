#include <sys/queue.h>

#include "tailq.h"
#include "mpscq.h"
#include "util.h"

static void
tailq_init_impl(struct mpscq_handle *hdl)
{
    struct tailq *q = from_mpscq(hdl);

    q->list = (struct tailq_list) TAILQ_HEAD_INITIALIZER(q->list);
    tailq_lock_init(&q->lock);
}

static bool
tailq_is_empty_impl(struct mpscq_handle *hdl)
{
    struct tailq *q = from_mpscq(hdl);
    bool empty;

    tailq_lock(&q->lock);
    empty = TAILQ_EMPTY(&q->list);
    tailq_unlock(&q->lock);

    return empty;
}

static void
tailq_insert_impl(struct mpscq_handle *hdl, union mpscq_node *node)
{
    struct tailq *q = from_mpscq(hdl);

    tailq_lock(&q->lock);
    TAILQ_INSERT_TAIL(&q->list, &node->tailq, node);
    tailq_unlock(&q->lock);
}

static inline union mpscq_node *
tailq_pop_impl(struct mpscq_handle *hdl)
{
    struct tailq *q = from_mpscq(hdl);
    struct tailq_node *node = NULL;

    tailq_lock(&q->lock);
    if (!TAILQ_EMPTY(&q->list)) {
        node = TAILQ_FIRST(&q->list);
        TAILQ_REMOVE(&q->list, node, node);
    }
    tailq_unlock(&q->lock);

    if (node != NULL) {
        return container_of(node, union mpscq_node, tailq);
    }
    return NULL;
}

static struct tailq static_tailq;

struct mpscq tailq = {
    .handle = to_mpscq(&static_tailq),
    .init = tailq_init_impl,
    .is_empty = tailq_is_empty_impl,
    .insert = tailq_insert_impl,
    .pop = tailq_pop_impl,
    .desc = "tailq",
};
