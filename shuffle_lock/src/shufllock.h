#ifndef __SHFLLOCK_H
#define __SHFLLOCK_H

struct spinlock {
	struct spinlock *next;
	union {
		int locked; /* 1 if lock acquired */
		struct {
			u8 lstatus;
			u8 sleader;
			u16 wcount;
		};
	};
	int count; /* nesting count */

	int nid;
	int cid;
	struct spinlock *last_visited;
};

static void shfl_spin_lock(struct spinlock *lock);

static void shfl_spin_unlock(struct spinlock *lock);

#endif /* __SHFLLOCK_H */