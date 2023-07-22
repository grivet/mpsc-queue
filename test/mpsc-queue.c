/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Gaëtan Rivet
 */

/* Primitives. */
#include "mpsc-queue.h"

/* Interface. */
#include "mpscq.h"

/* Implementation. */

#include "util.h"

static void
mpsc_queue_init_impl(struct mpscq_handle *hdl)
{
    mpsc_queue_init(from_mpscq(hdl));
}

static bool
mpsc_queue_is_empty_impl(struct mpscq_handle *hdl)
{
    return mpsc_queue_is_empty(from_mpscq(hdl));
}

static void
mpsc_queue_insert_impl(struct mpscq_handle *hdl, union mpscq_node *node)
{
    mpsc_queue_insert(from_mpscq(hdl), &node->dv);
}

static void
mpsc_queue_insert_batch_impl(struct mpscq_handle *hdl, size_t n_nodes,
                             union mpscq_node *node_ptrs[n_nodes])
{
    struct mpsc_queue_node *batch[n_nodes];

    for (size_t i = 0; i < n_nodes; i++) {
        batch[i] = &node_ptrs[i]->dv;
    }
    mpsc_queue_insert_batch(from_mpscq(hdl), n_nodes, batch);
}

static union mpscq_node *
mpsc_queue_pop_impl(struct mpscq_handle *hdl)
{
    struct mpsc_queue_node *node = mpsc_queue_pop(from_mpscq(hdl));

    if (node != NULL) {
        return container_of(node, union mpscq_node, dv);
    }
    return NULL;
}

static struct mpsc_queue static_mpsc_queue;

struct mpscq mpsc_queue = {
    .handle = to_mpscq(&static_mpsc_queue),
    .init = mpsc_queue_init_impl,
    .is_empty = mpsc_queue_is_empty_impl,
    .insert = mpsc_queue_insert_impl,
    .insert_batch = mpsc_queue_insert_batch_impl,
    .pop = mpsc_queue_pop_impl,
    .desc = "mpsc-queue",
};
