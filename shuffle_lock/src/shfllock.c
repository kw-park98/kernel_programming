#include <linux/module.h>
#include <linux/random.h>

#include "shfllock.h"

static DEFINE_PER_CPU_ALIGNED(struct qnode, qnode);

void shfl_spin_lock(struct shfllock *lock)
{
	struct qnode *prev, *next, *node;

	preempt_disable();

	if (!lock->locked && cmpxchg_relaxed(&lock->locked, _S_UNLOCK_VAL,
					     _S_LOCKED_VAL) == _S_UNLOCK_VAL)
		return;

	node = this_cpu_ptr(&qnode);

	node->status = S_WAITING;
	node->shuffler = false;
	node->next = NULL;
	node->batch = 0;
	node->node_id = smp_processor_id();

	prev = xchg(&lock->tail, node);
	if (prev)
		spin_until_very_next_waiter(lock, prev, node);
	else
		xchg_relaxed(&lock->no_stealing, true);

	for (;;) {
		if (node->batch == 0 || node->shuffler) {
			shuffle_waiters(lock, node, true);
		}

		while (lock->locked) {
			continue;
		}

		if (cmpxchg_relaxed(&lock->locked, _S_UNLOCK_VAL,
				    _S_LOCKED_VAL) == _S_UNLOCK_VAL)
			break;
	}

	next = READ_ONCE(node->next);
	if (!next) {
		if (cmpxchg_relaxed(&lock->tail, node, NULL) == node) {
			cmpxchg_relaxed(&lock->no_stealing, true, false);
			return;
		}

		while (!node->next)
			continue;

		next = node->next;
	}
	next->status = S_READY;
}

void shfl_spin_unlock(struct shfllock *lock)
{
	WRITE_ONCE(lock->locked, _S_UNLOCK_VAL);

	preempt_enable();
}

void spin_until_very_next_waiter(struct shfllock *lock, struct qnode *prev,
				 struct qnode *curr)
{
	prev->next = curr;

	for (;;) {
		if (curr->status == S_READY)
			return;

		if (curr->shuffler)
			shuffle_waiters(lock, curr, false);
	}
}

#define MAX_SHUFFLES 256

void shuffle_waiters(struct shfllock *lock, struct qnode *node,
		     bool vnext_waiter)
{
	struct qnode *curr, *prev, *next, *last;
	int batch;

	last = node;
	prev = node;
	curr = NULL;
	next = NULL;

	batch = node->batch;

	barrier();

	if (batch == 0)
		node->batch = ++batch;

	node->shuffler = false;
	if (batch >= MAX_SHUFFLES)
		return;

	for (;;) {
		curr = prev->next;
		if (!curr)
			break;
		if (curr == lock->tail)
			break;

		if (curr->node_id == node->node_id) {
			if (prev->node_id == node->node_id) {
				curr->batch = ++batch;
				last = curr;
				prev = curr;
			} else {
				next = curr->next;
				if (!next)
					break;

				curr->batch = ++batch;
				prev->next = next;
				curr->next = last->next;
				last->next = curr;
				last = curr;
			}
		} else {
			prev = curr;
		}

		if ((vnext_waiter && lock->locked == _S_UNLOCK_VAL) ||
		    (!vnext_waiter && node->status == S_READY))
			break;
	}

	last->shuffler = true;
}

MODULE_AUTHOR("Jeonghoon Lee");
MODULE_LICENSE("GPL");
