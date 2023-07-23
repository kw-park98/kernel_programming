#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/random.h>

#include "shufllock.h"

#define S_WAITING 0
#define S_READY 1
#define S_PARKED 2
#define S_SPINNING 3

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

#define MAX_NODES 4

static DEFINE_PER_CPU_ALIGNED(struct mcs_spinlock, mcs_nodes[MAX_NODES]);

/* Per-CPU pseudo-random number seed */
static DEFINE_PER_CPU(u32, seed);

static inline void set_sleader(struct spinlock *node, struct spinlock *qend)
{
	smp_store_release(&node->sleader, 1);
	if (qend != node)
		smp_store_release(&node->last_visited, qend);
}

static inline void clear_sleader(struct spinlock *node)
{
	node->sleader = 0;
}

static inline void set_waitcount(struct spinlock *node, int count)
{
	smp_store_release(&node->wcount, count);
}

/*
 * xorshift function for generating pseudo-random numbers:
 * https://en.wikipedia.org/wiki/Xorshift
 */
static inline u32 xor_random(void)
{
	u32 v;

	v = this_cpu_read(seed);
	if (v == 0)
		get_random_bytes(&v, sizeof(u32));

	v ^= v << 6;
	v ^= v >> 21;
	v ^= v << 7;
	this_cpu_write(seed, v);

	return v;
}

#define INTRA_SOCKET_HANDOFF_PROB_ARG 0x10000

static bool probably(void)
{
	u32 v;
	return xor_random() & (INTRA_SOCKET_HANDOFF_PROB_ARG - 1);
	v = this_cpu_read(seed);
	if (v >= 2048) {
		this_cpu_write(seed, 0);
		return false;
	}
	this_cpu_inc(seed);
	return true;
}

static void shuffle_waiters(struct spinlock *lock, struct spinlock *node,
			    int is_next_waiter)
{
	struct spinlock *curr, *prev, *next, *last, *sleader, *qend;
	int nid;
	int curr_locked_count;
	int one_shuffle = false;

	prev = smp_load_acquire(&node->last_visited);
	if (!prev)
		prev = node;
	last = node;
	curr = NULL;
	next = NULL;
	sleader = NULL;
	qend = NULL;

	nid = node->nid;
	curr_locked_count = node->wcount;

	barrier();

	/*
	 * If the wait count is 0, then increase node->wcount
	 * to 1 to avoid coming it again.
	 */
	if (curr_locked_count == 0) {
		set_waitcount(node, ++curr_locked_count);
	}

	clear_sleader(node);

	if (!probably()) {
		sleader = READ_ONCE(node->next);
		goto out;
	}

	for (;;) {
		curr = READ_ONCE(prev->next);

		if (!curr) {
			sleader = last;
			qend = prev;
			break;
		}

recheck_curr_tail:
		if (curr->cid ==
		    (atomic_read(&lock->val) >> _Q_TAIL_CPU_OFFSET)) {
			sleader = last;
			qend = prev;
			break;
		}

		/* Check if curr->nid is same as nid */
		if (curr->nid == nid) {
			/*
			 * if prev->nid == curr->nid, then
			 * just update the last and prev
			 * and proceed forward
			 */
			if (prev->nid == nid) {
				set_waitcount(curr, curr_locked_count);

				last = curr;
				prev = curr;
				one_shuffle = true;
			} else {
				/*
				 * prev->nid is not same, then we need
				 * to find next and move @curr to
				 * last->next, while linking @prev->next
				 * to next.
				 *
				 * NOTE: We do not unpdate @prev here
				 * because @curr has been already moved
				 * out.
				 */
				next = READ_ONCE(curr->next);
				if (!next) {
					sleader = last;
					qend = prev;
					/* qend = curr; */
					break;
				}

				set_waitcount(curr, curr_locked_count);

				prev->next = next;
				curr->next = last->next;
				last->next = curr;
				smp_wmb();

				last = curr;
				curr = next;
				one_shuffle = true;

				goto recheck_curr_tail;
			}
		} else
			prev = curr;

		/*
		 * Currently, we only exit once we have at least
		 * one shuffler if the shuffling leader is the
		 * very next lock waiter.
		 * TODO: This approach can be further optimized.
		 */
		if (one_shuffle) {
			if ((is_next_waiter &&
			     !(atomic_read_acquire(&lock->val) &
			       _Q_LOCKED_PENDING_MASK)) ||
			    (!is_next_waiter && READ_ONCE(node->lstatus))) {
				sleader = last;
				qend = prev;
				break;
			}
		}
	}

out:
	if (sleader) {
		set_sleader(sleader, qend);
	}
}

void queued_spin_lock_slowpath(struct spinlock *lock, u32 val)
{
	struct spinlock *prev, *next, *node;
	u32 old, tail;
	int idx;
	int cid;

	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));

	/*
	 * Wait for in-progress pending->locked hand-overs with a bounded
	 * number of spins so that we guarantee forward progress.
	 *
	 * 0,1,0 -> 0,0,1
	 */
	if (val == _Q_PENDING_VAL) {
		int cnt = _Q_PENDING_LOOPS;
		;
		val = atomic_cond_read_relaxed(
			&lock->val, (VAL != _Q_PENDING_VAL) || !cnt--);
	}

	/*
	 * If we observe any contention; queue.
	 */
	if (val & ~_Q_LOCKED_MASK)
		goto queue;

	/*
	 * trylock || pending
	 *
	 * 0,0,0 -> 0,0,1 ; trylock
	 * 0,0,1 -> 0,1,1 ; pending
	 */
	val = atomic_fetch_or_acquire(_Q_PENDING_VAL, &lock->val);
	if (!(val & ~_Q_LOCKED_MASK)) {
		/*
		 * We're pending, wait for the owner to go away.
		 *
		 * *,1,1 -> *,1,0
		 *
		 * this wait loop must be a load-acquire such that we match the
		 * store-release that clears the locked bit and create lock
		 * sequentiality; this is becuase not all
		 * clear_pending_set_locked() implementations imply full
		 * barriers.
		 */
		if (val & _Q_LOCKED_MASK) {
			atomic_cond_read_acquire(&lock->val,
						 !(VAL & _Q_LOCKED_MASK));
		}

		/*
		 * take ownership and clear the pending bit.
		 *
		 * *,1,0 -> *,0,1
		 */
		clear_pending_set_locked(lock);
		qstat_inc(qstat_lock_pending, true);
		return;
	}

	/* If pending was clear but there are waiters in the queue, then
	 * we need to undo our setting of pending before we queue ourselves.
	 */
	if (!(val & _Q_PENDING_MASK))
		clear_pending(lock);

	/*
	 * End of pending bit optimistic spinning and beginning of MCS
	 * queueing.
	 */
queue:
	qstat_inc(qstat_lock_slowpath, true);
pv_queue:
	node = this_cpu_ptr(&mcs_nodes[0]);
	idx = node->count++;
	cid = smp_processor_id();
	tail = encode_tail(cid, idx);

	node += idx;

	/*
	 * Ensure that we increment the head node->count before initialising
	 * the actual node. If the compiler is kind enough to reorder these
	 * stores, then an IRQ could overwrite our assignments.
	 */
	barrier();

	node->cid = cid + 1;
	node->nid = numa_node_id();
	node->last_visited = NULL;
	node->locked = 0;
	node->next = NULL;
	pv_init_node(node);

	/*
	 * We touched a (possibly) cold cacheline in the per-cpu queue node;
	 * attempt the trylock once more in the hope someone let go while we
	 * weren't watching.
	 */
	if (queued_spin_trylock(lock))
		goto release;

	/*
	 * Ensure that the initialisation of @node is complete before we
	 * publish the updated tail via xchg_tail() and potentially link
	 * @node into the waitqueue via WRITE_ONCE(prev->next, node) below. 
	 */
	smp_wmb();

	/*
	 * Publish the updated tail.
	 * We have already touched the queueing cacheline; don't bother with
	 * pending stuff.
	 *
	 * p,*,* -> n,*,*
	 */
	old = xchg_tail(lock, tail);
	next = NULL;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_MASK) {
		prev = decode_tail(old);

		/* Link @node into the waitqueue. */
		WRITE_ONCE(prev->next, node);

		pv_wait_node(node, prev);

		for (;;) {
			int __val = READ_ONCE(node->lstatus);
			if (__val)
				break;

			if (READ_ONCE(node->sleader))
				shuffle_waiters(lock, node, false);

			cpu_relax();
		}
		smp_acquire__after_ctrl_dep();

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We optimistically load
		 * the next pointer & prefetch the cacheline for writing
		 * to reduce latency in the upcoming MCS unlock operation.
		 */
		/* next = READ_ONCE(node->next); */
		/* if (next) */
		/* 	prefetchw(next); */
	}

	/*
	 * we're at the head of the waitqueue, wait for the owner & pending to
	 * go away.
	 *
	 * *,x,y -> *,0,0
	 *
	 * this wait loop must use a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because the set_locked() function below
	 * does not imply a full barrier.
	 *
	 * The PV pv_wait_head_or_lock function, if active, will acquire
	 * the lock and return a non-zero value. So we have to skip the
	 * atomic_cond_read_acquire() call. As the next PV queue head hasn't
	 * been designated yet, there is no way for the locked value to become
	 * _Q_SLOW_VAL. So both the set_locked() and the
	 * atomic_cmpxchg_relaxed() calls will be safe.
	 *
	 * If PV isn't active, 0 will be returned instead.
	 *
	 */
	/* if ((val = pv_wait_head_or_lock(lock, node))) */
	/* 	goto locked; */

	/* val = atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_PENDING_MASK)); */
	for (;;) {
		int wcount;

		val = atomic_read(&lock->val);
		if (!(val & _Q_LOCKED_PENDING_MASK))
			break;

		wcount = READ_ONCE(node->wcount);
		if (!wcount || (wcount && node->sleader))
			shuffle_waiters(lock, node, true);
		cpu_relax();
	}
	smp_acquire__after_ctrl_dep();

locked:
	/*
	 * claim the lock:
	 *
	 * n,0,0 -> 0,0,1 : lock, uncontended
	 * *,*,0 -> *,*,1 : lock, contended
	 *
	 * If the queue head is the only one in the queue (lock value == tail)
	 * and nobody is pending, clear the tail code and grab the lock.
	 * Otherwise, we only need to grab the lock.
	 */

	/*
	 * In the PV case we might already have _Q_LOCKED_VAL set.
	 *
	 * The atomic_cond_read_acquire() call above has provided the
	 * necessary acquire semantics required for locking.
	 */
	if (((val & _Q_TAIL_MASK) == tail) &&
	    atomic_try_cmpxchg_relaxed(&lock->val, &val, _Q_LOCKED_VAL))
		goto release; /* No contention */

	/* Either somebody is queued behind us or _Q_PENDING_VAL is set */
	set_locked(lock);

	/*
	 * contended path; wait for next if not observed yet, release.
	 */
	next = READ_ONCE(node->next);
	if (!next)
		next = smp_cond_load_relaxed(&node->next, (VAL));

	/* arch_mcs_spin_unlock_contended(&next->locked); */
	smp_store_release(&next->lstatus, 1);
	pv_kick_node(lock, next);

release:
	/*
	 * release the node
	 */
	__this_cpu_dec(mcs_nodes[0].count);
}
EXPORT_SYMBOL(queued_spin_lock_slowpath);

static void queued_spin_lock(struct spinlock *lock)
{
	int val = 0;

	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL)))
		return;

	queued_spin_lock_slowpath(lock, val);
}
EXPORT_SYMBOL(queued_spin_lock);

static void queued_spin_unlock(struct spinlock *lock)
{
	smp_store_release(&lock->locked, 0);
}
EXPORT_SYMBOL(queued_spin_unlock);

MODULE_AUTHOR("Jeonghoon Lee");
MODULE_LICENSE("GPL");
