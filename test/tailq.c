#include <sys/queue.h>

#include "tailq.h"
#include "mpscq.h"
#include "util.h"

#define TAILQ_MERGE(q1, q2, field) do {                       \
        if((q2)->tqh_first) {                                 \
            *(q1)->tqh_last = (q2)->tqh_first;                \
            (q2)->tqh_first->field.tqe_prev = (q1)->tqh_last; \
            (q1)->tqh_last = (q2)->tqh_last;                  \
            TAILQ_INIT(q2);                                   \
        }                                                     \
    } while(0)

static void
tailq_init_impl(struct mpscq_handle *hdl)
{
    struct tailq *q = from_mpscq(hdl);

    TAILQ_INIT(&q->plist);
    TAILQ_INIT(&q->clist);
    tailq_lock_init(&q->lock);
}

static bool
tailq_is_empty_impl(struct mpscq_handle *hdl)
{
    struct tailq *q = from_mpscq(hdl);
    bool empty;

    if (!TAILQ_EMPTY(&q->clist)) {
        return false;
    }

    tailq_lock(&q->lock);
    empty = TAILQ_EMPTY(&q->plist);
    tailq_unlock(&q->lock);

    return empty;
}

static void
tailq_insert_impl(struct mpscq_handle *hdl, union mpscq_node *node)
{
    struct tailq *q = from_mpscq(hdl);

    tailq_lock(&q->lock);
    TAILQ_INSERT_TAIL(&q->plist, &node->tailq, node);
    tailq_unlock(&q->lock);
}

static void
tailq_insert_batch_impl(struct mpscq_handle *hdl, size_t n_nodes,
                        union mpscq_node *node_ptrs[n_nodes])
{
    struct tailq *q = from_mpscq(hdl);
    struct tailq_list batch;

    TAILQ_INIT(&batch);
    for (size_t i = 0; i < n_nodes; i++) {
        TAILQ_INSERT_TAIL(&batch, &node_ptrs[i]->tailq, node);
    }

    tailq_lock(&q->lock);
    TAILQ_MERGE(&q->plist, &batch, node);
    tailq_unlock(&q->lock);
}

static inline union mpscq_node *
tailq_pop_impl(struct mpscq_handle *hdl)
{
    struct tailq *q = from_mpscq(hdl);
    struct tailq_node *node = NULL;

    if (TAILQ_EMPTY(&q->clist)) {
        tailq_lock(&q->lock);
        TAILQ_MERGE(&q->clist, &q->plist, node);
        tailq_unlock(&q->lock);
    }

    if (!TAILQ_EMPTY(&q->clist)) {
        node = TAILQ_FIRST(&q->clist);
        TAILQ_REMOVE(&q->clist, node, node);
    }

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
    .insert_batch = tailq_insert_batch_impl,
    .pop = tailq_pop_impl,
    .desc = "tailq",
};
