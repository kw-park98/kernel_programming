/**
 * benchmark.c - benchmark to check throughputs per ncores
 *
 * Run benchmark from 1 core to MAX-1 cores
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "worker.h"

/* Global variables */
static struct worker *workers;
static int duration = 2000;
module_param(duration, int, 0600);

/* Module functions */
int worker_init(struct worker *worker, int (*threadfn)(void *data))
{
	static int id = 0;
	worker->id = ++id;
	worker->task =
		kthread_create(threadfn, worker, "kthread-%d", worker->id);
	if (IS_ERR(worker->task)) {
		printk("benchmark - Thread[%d] could not be created!\n",
		       worker->id);
		goto fail;
	}
	worker->retval = 0;
	return 0;
fail:
	worker->id = -1;
	return -1;
}

static int thread_function(void *data)
{
	long count = 0;
	struct worker *worker = data;

	while (!kthread_should_stop()) {
		worker_routine();
		count += 1;
	}
	worker->retval = count;
	printk("benchmark - Thread[%d]: %ld ops\n", worker->id, count);
	return 0;
}

/**
 * benchmark_main(n)
 *
 * @n: number of workers
 *
 * This function creates n workers and check total throughput of workers
 * during specific duration.
 *
 * Return: error code
 */
static int benchmark_main(int n)
{
	struct worker *worker;
	long throughput = 0;
	int err;

	printk("benchmark - Start: %d core(s), %d ms\n", n, duration);

	worker_setup();
	for (int i = 0; i < n; i++) {
		worker = &workers[i];
		err = worker_init(worker, thread_function);
		if (unlikely(err != 0))
			goto fail;
		wake_up_process(worker->task);
	}
	msleep(duration);
	for (int i = 0; i < n; i++) {
		worker = &workers[i];
		kthread_stop(worker->task);
		throughput += worker->retval;
	}
	printk("benchmark -   Throughput : %lu ops\n", throughput);
	printk("benchmark -   Work Result: %lu\n", worker_teardown());
	return 0;
fail:
	return -1;
}

static int __init start_module(void)
{
	const int ncores = num_online_cpus();
	int n = 1;
	int err;

	printk(KERN_INFO "Loading module...\n");
	workers = kmalloc(sizeof(struct worker) * ncores, GFP_KERNEL);
	while (n < ncores) {
		err = benchmark_main(n);
		if (unlikely(err != 0))
			goto fail;
		n++;
	}
	return 0;
fail:
	return -1;
}

static void __exit end_module(void)
{
	kfree(workers);
	printk(KERN_INFO "Unloading module...\n");
}

MODULE_AUTHOR("Hyeonmin Lee");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
