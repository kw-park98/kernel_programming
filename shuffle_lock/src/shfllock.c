#include <linux/module.h>
#include <linux/random.h>

#include "shfllock.h"

static DEFINE_PER_CPU_ALIGNED(qnode_t, qnode);

void shfl_spin_lock(shfllock_t *lock)
{
	qnode_t *prev, *next, *node;

	preempt_disable();

	if (cmpxchg(&lock->val, _S_UNLOCK_VAL, _S_LOCKED_VAL) == _S_UNLOCK_VAL)
		return;

	node = this_cpu_ptr(&qnode);

	WRITE_ONCE(node->lstatus, S_WAITING);
	WRITE_ONCE(node->sleader, false);
	WRITE_ONCE(node->wcount, 0);
	WRITE_ONCE(node->next, NULL);
	WRITE_ONCE(node->cid, 1); /* TODO: change to random. */

	prev = xchg(&lock->tail, node);
	if (prev)
		spin_until_very_next_waiter(lock, prev, node);
	else
		xchg(&lock->no_stealing, true);

	for (;;) {
		if (READ_ONCE(node->wcount) == 0 || READ_ONCE(node->sleader)) {
			shuffle_waiters(lock, node, true);
		}

		while (READ_ONCE(lock->locked) == _S_LOCKED_VAL) {
			continue;
		}

		if (cmpxchg(&lock->locked, _S_UNLOCK_VAL, _S_LOCKED_VAL) ==
		    _S_UNLOCK_VAL)
			break;
	}

	next = READ_ONCE(node->next);
	if (!next) {
		if (cmpxchg(&lock->tail, node, NULL) == node) {
			cmpxchg(&lock->no_stealing, true, false);
			return;
		}

		while (!READ_ONCE(node->next))
			continue;

		WRITE_ONCE(next, node->next);
	}
	WRITE_ONCE(next->lstatus, S_READY);
}

void shfl_spin_unlock(shfllock_t *lock)
{
	WRITE_ONCE(lock->locked, _S_UNLOCK_VAL);

	preempt_enable();
}

void spin_until_very_next_waiter(shfllock_t *lock, qnode_t *prev, qnode_t *curr)
{
	WRITE_ONCE(prev->next, curr);

	for (;;) {
		if (READ_ONCE(curr->lstatus) == S_READY)
			return;

		if (READ_ONCE(curr->sleader))
			shuffle_waiters(lock, curr, false);
	}
}

#define MAX_SHUFFLES 3

void shuffle_waiters(shfllock_t *lock, qnode_t *node, bool vnext_waiter)
{
	qnode_t *curr, *prev, *next, *last;
	int batch;

	WRITE_ONCE(last, node);
	WRITE_ONCE(prev, node);
	WRITE_ONCE(curr, NULL);
	WRITE_ONCE(next, NULL);

	WRITE_ONCE(batch, node->wcount);

	barrier();

	if (READ_ONCE(batch) == 0)
		WRITE_ONCE(node->wcount, ++batch);

	WRITE_ONCE(node->sleader, false);
	if (READ_ONCE(batch) >= MAX_SHUFFLES)
		return;

	for (;;) {
		WRITE_ONCE(curr, prev->next);
		if (!READ_ONCE(curr))
			break;
		if (READ_ONCE(curr) == READ_ONCE(lock->tail))
			break;

		if (curr->cid == node->cid) {
			if (prev->cid == node->cid) {
				WRITE_ONCE(curr->wcount, ++batch);
				WRITE_ONCE(last, curr);
				WRITE_ONCE(prev, curr);
			} else {
				WRITE_ONCE(next, READ_ONCE(curr->next));
				if (!READ_ONCE(next))
					break;

				WRITE_ONCE(curr->wcount, ++batch);
				WRITE_ONCE(prev->next, next);
				WRITE_ONCE(curr->next, last->next);
				WRITE_ONCE(last->next, curr);
				WRITE_ONCE(last, curr);
			}
		} else {
			WRITE_ONCE(prev, curr);
		}

		if ((vnext_waiter &&
		     READ_ONCE(lock->locked) == _S_UNLOCK_VAL) ||
		    (!vnext_waiter && READ_ONCE(node->lstatus) == S_READY))
			break;
	}

	WRITE_ONCE(last->sleader, true);
}

MODULE_AUTHOR("Jeonghoon Lee");
MODULE_LICENSE("GPL");
