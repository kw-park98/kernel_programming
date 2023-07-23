/* SPDX-License-Identifier: GPL-2.0 */

/**
 * worker.c - worker for benchmark.c
 */

#include <linux/atomic.h>
#include <asm-generic/qspinlock_types.h>

#include "worker.h"
#include "shufllock.h"

/**
 * Shared Variables
 */
/* Change below */
static atomic_long_t counter;
static struct spinlock s_lock = __ARCH_SPIN_LOCK_UNLOCKED;

/**
 * worker_setup() - test setup
 *
 * This function is for setting up the benchmark.
 * I recommend to initialize shared variables here.
 *
 * Example: value = 0, file_open(), ...
 *
 * Context: Benchmark context. It is called by main thread per a benchmark.
 */
void worker_setup(void)
{
	/* Change below */
	atomic_long_set(&counter, 0);
}

/**
 * worker_routine() - routine per worker thread
 *
 * The benchmark checks how many times this function is called during
 * specific duration. And worker threads call this function  in parallel.
 *
 * Context: Thread context. It is called by worker threads many times.
 */
void worker_routine(void)
{
	/* Change below */
	queued_spin_lock(&s_lock);
	counter++;
	queued_spin_unlock(&s_lock);
	// atomic_long_inc(&counter);
}

/**
 * worker_teardown() - test teardown
 *
 * This function is for finishing the benchmark.
 * 
 * Example: file_close(), ... 
 *
 * Context: Benchmark context. It is called by main thread per a benchmark.
 *
 * Return: Any value which you want to check consistency. 
 */
long worker_teardown(void)
{
	/* Change below */
	return atomic_long_read(&counter);
}
