#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include "mpscq.h"
#include "unit.h"
#include "util.h"

struct element {
    unsigned int id;
    union mpscq_node node;
};

static void
test_mpscq_insert(struct mpscq *q)
{
    union mpscq_node *node;
    struct element elements[10];
    size_t i;

    mpscq_init(q);

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        elements[i].id = i;
        mpscq_insert(q, &elements[i].node);
    }

    assert(mpscq_is_empty(q) == false);

    while ((node = mpscq_pop(q))) {
        struct element *e = container_of(node, struct element, node);
        assert(e->id == (unsigned int)(e - elements));
    }
}

int main(void)
{
    test_mpscq_insert(&ts_mpsc_queue);
    test_mpscq_insert(&tailq);
    test_mpscq_insert(&mpsc_queue);
    test_mpsc_queue();
    return 0;
}
