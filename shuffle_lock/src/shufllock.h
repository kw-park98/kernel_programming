#ifndef __SHFLLOCK_H
#define __SHFLLOCK_H

static void queued_spin_lock(struct spinlock *lock);

static void queued_spin_unlock(struct spinlock *lock);

#endif /* __SHFLLOCK_H */