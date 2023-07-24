#ifndef __SHFLLOCK_H
#define __SHFLLOCK_H

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/atomic.h>

#define S_WAITING 0
#define S_READY 1
#define S_PARKED 2
#define S_SPINNING 3

typedef struct shfllock {
	u8 locked; /* 1 if lock acquired */
	u8 no_stealing;

	u8 reserved[2];

	struct qnode *tail;
} shfllock_t;

struct qnode {
	u8 shuffler;
	u8 node_id;
	u8 status;

	u8 reserved[1];

	int batch;

	struct qnode *next;
};

void shfl_spin_lock(struct shfllock *lock);

void shfl_spin_unlock(struct shfllock *lock);

void spin_until_very_next_waiter(struct shfllock *lock, struct qnode *prev,
				 struct qnode *curr);

void shuffle_waiters(struct shfllock *lock, struct qnode *node,
		     bool vnext_waiter);

#define _S_UNLOCK_VAL (0)
#define _S_LOCKED_VAL (1U)

#ifdef ALLOW_STEALING
#define _S_DEFAULT_NO_STEALING 0
#else
#define _S_DEFAULT_NO_STEALING 1
#endif

#define __SHFL_LOCK_INITIALIZER(lockname)                          \
	{                                                          \
		.locked = 0, .no_stealing = _S_DEFAULT_NO_STEALING \
	}

#define __SHFL_LOCK_UNLOCKED(lockname) \
	(shfllock_t) __SHFL_LOCK_INITIALIZER(lockname)

#define DEFINE_SHFLLOCK(x) shfllock_t x = __SHFL_LOCK_UNLOCKED(x)

#endif /* __SHFLLOCK_H */
