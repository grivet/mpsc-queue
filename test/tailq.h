#ifndef TAILQ_H
#define TAILQ_H

#include <pthread.h>
#include <sys/queue.h>

#if __APPLE__
/* No Spinlock on macos. */
#define USE_FAIR_LOCK
#endif
#define USE_FAIR_LOCK

#ifdef USE_FAIR_LOCK
#define tailq_lock_type pthread_mutex_t
#define tailq_lock_init(l) do { pthread_mutex_init(l, NULL); } while (0)
#define tailq_lock_destroy(l) do { pthread_mutex_destroy(l); } while (0)
#define tailq_lock(l) do { pthread_mutex_lock(l); } while (0)
#define tailq_unlock(l) do { pthread_mutex_unlock(l); } while (0)
#else
#define tailq_lock_type pthread_spinlock_t
#define tailq_lock_init(l) do { pthread_spin_init(l, PTHREAD_PROCESS_PRIVATE); } while (0)
#define tailq_lock_destroy(l) do { pthread_spin_destroy(l); } while (0)
#define tailq_lock(l) do { pthread_spin_lock(l); } while (0)
#define tailq_unlock(l) do { pthread_spin_unlock(l); } while (0)
#endif

struct tailq_node {
    TAILQ_ENTRY(tailq_node) node;
};

TAILQ_HEAD(tailq_list, tailq_node);

struct tailq {
    /* Producer's list. */
    struct tailq_list plist;
    /* Consumer list. */
    struct tailq_list clist;
    tailq_lock_type lock;
};

#endif /* TAILQ_H */
