/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Gaëtan Rivet
 */

#ifndef MPSCQ_H
#define MPSCQ_H

#include <stdbool.h>

#include "tailq.h"
#include "mpsc-queue.h"
#include "ts-mpsc-queue.h"

union mpscq_node {
    struct tailq_node tailq;
    struct ts_mpsc_queue_node ts;
    struct mpsc_queue_node dv;
};

struct mpscq_handle;

struct mpscq {
    struct mpscq_handle *handle;
    void (*init)(struct mpscq_handle *q);
    bool (*is_empty)(struct mpscq_handle *q);
    void (*insert)(struct mpscq_handle *q, union mpscq_node *node);
    void (*insert_batch)(struct mpscq_handle *q, size_t n_nodes,
                         union mpscq_node *node_ptrs[n_nodes]);
    union mpscq_node *(*pop)(struct mpscq_handle *q);
    const char *desc;
};

#define from_mpscq(handle) ((void *) (handle))
#define to_mpscq(q) ((void *) (q))

static inline void
mpscq_init(struct mpscq *q)
{
    q->init(q->handle);
}

static inline bool
mpscq_is_empty(struct mpscq *q)
{
    return q->is_empty(q->handle);
}

static inline void
mpscq_insert(struct mpscq *q, union mpscq_node *node)
{
    q->insert(q->handle, node);
}

static inline void
mpscq_insert_batch(struct mpscq *q, size_t n_nodes,
                   union mpscq_node *node_ptrs[n_nodes])
{
    if (q->insert_batch) {
        q->insert_batch(q->handle, n_nodes, node_ptrs);
    } else {
        for (size_t i = 0; i < n_nodes; i++) {
            mpscq_insert(q, node_ptrs[i]);
        }
    }
}

static inline union mpscq_node *
mpscq_pop(struct mpscq *q)
{
    return q->pop(q->handle);
}

extern struct mpscq mpsc_queue;
extern struct mpscq ts_mpsc_queue;
extern struct mpscq tailq;

#endif /* MPSCQ_H */
