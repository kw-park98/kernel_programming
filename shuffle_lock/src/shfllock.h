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

typedef struct qspinlock_s {
	union {
		atomic_t val;
		struct {
			u8 locked;
			u8 no_stealing;
		};
	};
	qnode_t *tail;
} shfllock_t;

typedef struct mcs_spinlock_s {
	qnode_t *next;
	union {
		int locked;
		struct {
			u8 lstatus;
			u8 sleader;
			u16 wcount;
		}
	};

	int cid;
	struct mcs_spinlock *last_visited;
} qnode_t;

void shfl_spin_lock(shfllock_t *lock);

void shfl_spin_unlock(shfllock_t *lock);

void spin_until_very_next_waiter(shfllock_t *lock, qnode_t *prev,
				 qnode_t *curr);

void shuffle_waiters(shfllock_t *lock, qnode_t *node, bool vnext_waiter);

#define _S_UNLOCK_VAL (0)
#define _S_LOCKED_VAL (1U)

#define __SHFL_LOCK_INITIALIZER(lockname) \
	{                                 \
		.val = 0                  \
	}

#define __SHFL_LOCK_UNLOCKED(lockname) \
	(shfllock_t) __SHFL_LOCK_INITIALIZER(lockname)

#define DEFINE_SHFLLOCK(x) shfllock_t x = __SHFL_LOCK_UNLOCKED(x)

#endif /* __SHFLLOCK_H */
