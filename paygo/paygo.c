#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <linux/hash.h>
#include <linux/sched.h>
#include "paygo.h"

// TODO
//
// 0. change the code that init(kmalloc -> DEFINE_PERCPU)
//
// 1. Do we need to initialize hashtable entry's list every time we create a null entry? ( INIT_LIST_HEAD(&(entry->list)); )
//
//

//////////////////////////////////////////////////////////////////////////////////////////
// custom number

// size of the per-cpu hashtable
#define TABLESIZE (4)

// hash function shift bits
#define HASHSHIFT (11)

// the number of test kthread
#define NTHREAD (8)

// the number of objs to test
#define NOBJS (10)

//////////////////////////////////////////////////////////////////////////////////////////
struct thread_data {
	int thread_id;
};

static int thread_fn(void *data);
static struct task_struct *threads[NTHREAD];

// This is the data structure that will be used in place of tast struct.
static struct thread_data thread_datas[NTHREAD];

atomic_t thread_done;
static int cpu_ops_ref[128];
static int cpu_ops_unref_local[128];
static int cpu_ops_unref_other[128];

static int thread_ops[NTHREAD];

static void *objs[NOBJS];

//////////////////////////////////////////////////////////////////////////////////////////
//data structures
struct paygo_entry {
	void *obj;
	int local_counter;
	atomic_t anchor_counter;
	struct list_head list;


	// x,y are used for debug
	// x means local inc to the obj
	// y means local dec to the obj

	// (x + y) = entry->local_counter (always)
	// (x + y) + anchor_counter = 0 when the program end (i.e. After all inc and dec pairs have been terminated)
	int x;
	int y;
} ____cacheline_aligned_in_smp;

struct overflow {
	spinlock_t lock;
	struct list_head head;
};

struct paygo {
	struct paygo_entry entries[TABLESIZE];
	struct overflow overflow_lists[TABLESIZE];
};

DEFINE_PER_CPU(struct paygo *, paygo_table_ptr);
//////////////////////////////////////////////////////////////////////////////////////////
// private functions

/*
-------------------------------------------------------
These functions are used for setting up.

1. hash_function
-------------------------------------------------------
These functions are used in hashtable.

2. push_hash 
3. find_hash
-------------------------------------------------------
These functions are used for anchor information to put and get.

4. record_anchor
5. unrecord_anchor
-------------------------------------------------------
This function is used in paygo_dec when decrement other cpu's hashtable entry.

6. dec_other_entry

*/

/**
 * hash_function
 *
 * @obj: pointer of the object
 *
 * Return: index of the object in the PCP-hashtable
 */
static unsigned long hash_function(const void *obj);

/**
 * push_hash
 *
 * @obj: pointer of the object
 *
 * Return: 0 for success
 *
 * Push paygo entry into the per-cpu hashtable.
 *
 */
static int push_hash(void *obj);

/**
 * find_hash
 *
 * @obj: pointer of the object
 *
 * Return: pointer of the paygo entry from the per-cpu hashtable.
 *
 * Find paygo entry from the per-cpu hashtable.
 * And delete entry of the hash table when if the hash table's entry's total count is zero.
 */
static struct paygo_entry *find_hash(void *obj);

/**
 * record_anchor
 *
 * @cpu: id of the processor
 * @thread_id: id of the task (Not a pid. this is the logical number of the thread)
 *
 * Context: used in paygo_inc
 *
 * Record the CPU ID of the processor on which PAYGO_REF was executed to the tast struct. 
 *
 */
static void record_anchor(int thread_id, int cpu, void *obj);

/**
 * unrecord_anchor
 *
 * @thread_id: id of the task (Not a pid. this is the logical number of the thread)
 *
 * Context: used in paygo_dec
 *
 * Get the CPU ID of the processor on which PAYGO_REF was *LAST* executed.
 *
 */
static int unrecord_anchor(int thread_id, void *obj);

/**
 * dec_other_entry
 *
 * @obj: pointer of the object
 * @cpu: id of the processor
 *
 *
 * Decrement the given cpu's anchor counter.
 *
 */
static void dec_other_entry(void *obj, int cpu);

//////////////////////////////////////////////////////////////////////////////////////////

static int __init start_module(void)
{
	int i;
	int cpu;
	pr_info("paygo module loaded!\n");
	pr_info("paygo entry size = %lu\n", sizeof(struct paygo_entry));

	//initialize for the test
	atomic_set(&thread_done, 0);
	for (i = 0; i < NOBJS; i++) {
		objs[i] = kzalloc(sizeof(void *), GFP_KERNEL);
	}
	for (i = 0; i < NTHREAD; i++) {
		thread_datas[i].thread_id = i;
	}
	for_each_possible_cpu(cpu) {
		cpu_ops_ref[cpu] = 0;
		cpu_ops_unref_local[cpu] = 0;
		cpu_ops_unref_other[cpu] = 0;

		cpu_ops_ref[cpu + 1] = -1;
		cpu_ops_unref_local[cpu + 1] = -1;
		cpu_ops_unref_other[cpu + 1] = -1;
	}

	//initialize the per-cpu hashtable
	init_paygo_table();

	for (i = 0; i < NTHREAD; i++) {
		threads[i] = kthread_run(thread_fn, &thread_datas[i],
					 "my_kthread%d", i);
		if (IS_ERR(threads[i])) {
			printk(KERN_ERR "Failed to create counter thread.\n");
			return PTR_ERR(threads[i]);
		}
	}

	return 0;
}

static void __exit end_module(void)
{
	int i;
	for (i = 0; i < NTHREAD; i++) {
		if (threads[i]) {
			kthread_stop(threads[i]);
			threads[i] = NULL;
		}
	}
	// wait for all of the threads to finish
	while (atomic_read(&thread_done) < NTHREAD)
		;

	traverse_paygo();

	for (i = 0; i < NOBJS; i++) {
		// it must print only true in this step.
		pr_info("READALL: %d\n", paygo_read(objs[i]));
	}

	// free the test objs
	for (i = 0; i < NOBJS; i++) {
		kfree(objs[i]);
	}
	pr_info("paygo module removed!\n");
}

void init_paygo_table(void)
{
	int cpu;
	struct paygo *p;

	// initialize all of cpu's hashtable
	for_each_possible_cpu(cpu) {
		p = kzalloc(sizeof(struct paygo), GFP_KERNEL);
		if (!p) {
			pr_err("Failed to allocate paygo table for CPU %d\n",
			       cpu);
			continue;
		}

		per_cpu(paygo_table_ptr, cpu) = p;

		for (int j = 0; j < TABLESIZE; j++) {
			p->entries[j].obj = NULL;
			p->entries[j].local_counter = 0;
			atomic_set(&p->entries[j].anchor_counter, 0);
			spin_lock_init(&p->overflow_lists[j].lock);
			INIT_LIST_HEAD(&p->overflow_lists[j].head);
		}
	}
}
EXPORT_SYMBOL(init_paygo_table);

static unsigned long hash_function(const void *obj)
{
	unsigned long ret;
	unsigned long hash;
	hash = hash_64((unsigned long)obj, HASHSHIFT);
	ret = hash % TABLESIZE;
	return ret;
}

static int push_hash(void *obj)
{
	unsigned long hash;
	struct paygo_entry *entry;
	struct overflow *ovfl;
	struct paygo *p = per_cpu(paygo_table_ptr, smp_processor_id());

	hash = hash_function(obj);
	entry = &p->entries[hash];

	// when the hashtable's entry is NULL
	if (entry->obj == NULL) {
		{
			entry->obj = obj;
			entry->local_counter = 1;
			atomic_set(&entry->anchor_counter, 0);

			entry->x = 1;
			entry->y = 0;
		}
		return 0;
	}

	// when there is NULL in the entry
	// We need to insert a new entry into the overflow list
	else {
		struct paygo_entry *new_entry;
		ovfl = &p->overflow_lists[hash];
		new_entry = kzalloc(sizeof(struct paygo_entry), GFP_ATOMIC);
		if (!new_entry) {
			return -ENOMEM;
		}
		{
			new_entry->obj = obj;
			new_entry->local_counter = 1;
			atomic_set(&new_entry->anchor_counter, 0);

			new_entry->x = 1;
			new_entry->y = 0;
		}

		spin_lock(&ovfl->lock);
		list_add(&new_entry->list, &ovfl->head);
		spin_unlock(&ovfl->lock);
		return 0;
	}
}

static struct paygo_entry *find_hash(void *obj)
{
	int cpu;
	unsigned long hash;
	struct paygo_entry *entry;
	struct overflow *ovfl;
	struct list_head *pos, *n;
	struct paygo *p;
	cpu = smp_processor_id();
	p = per_cpu(paygo_table_ptr, cpu);

	hash = hash_function(obj);

	entry = &p->entries[hash];

// redo to delete a new entry when the sum of the new entry's counter taken from the overflow list is 0
redo:
	if (likely(entry->obj == obj)) {
		return entry;
	} else {
		ovfl = &p->overflow_lists[hash];
		spin_lock(&ovfl->lock);
		///////////////////////////////////////////////////////////////////////////////////////////
		// delete entry on hash table
		if (unlikely(entry->local_counter +
				     atomic_read(&(entry->anchor_counter)) ==
			     0)) {
			struct paygo_entry *new_entry;
			if (!list_empty(&ovfl->head)) {
				new_entry = list_first_entry(
					&ovfl->head, struct paygo_entry, list);
				*entry = *new_entry;
				list_del(&new_entry->list);
				kfree(new_entry);
				spin_unlock(&ovfl->lock);
				// we need to redo and check
				// 0. Whether or not a new entry in the hashtable is what we were looking for
				// 1. Whether or not the overflow list is empty
				goto redo;
			}

			else {
				entry->obj = NULL;
				entry->local_counter = 0;
				atomic_set(&(entry->anchor_counter), 0);
				entry->x = 0;
				entry->y = 0;

				spin_unlock(&ovfl->lock);
				return NULL;
			}
		}
		///////////////////////////////////////////////////////////////////////////////////////////

		// the hashtable's entry is not the entry that we are looking for.
		// so we need to search overflow list
		list_for_each_safe(pos, n, &ovfl->head) {
			struct paygo_entry *ovfl_entry =
				list_entry(pos, struct paygo_entry, list);

			if (ovfl_entry->obj == obj) {
				spin_unlock(&ovfl->lock);
				return ovfl_entry;
			}
		}
		spin_unlock(&ovfl->lock);
	}

	return NULL;
}

int paygo_inc(void *obj, int thread_id)
{
	int ret;
	int cpu;
	struct paygo_entry *entry;
	cpu = get_cpu();

	cpu_ops_ref[cpu] += 1;

	entry = find_hash(obj);
	// if there is an entry!
	if (entry) {
		entry->local_counter += 1;
		entry->x += 1;
		record_anchor(thread_id, cpu, obj);
		ret = 0;
		put_cpu();
		return ret;
	}

	// if there isn't
	ret = push_hash(obj);
	record_anchor(thread_id, cpu, obj);
	put_cpu();
	return ret;
}
EXPORT_SYMBOL(paygo_inc);

int paygo_dec(void *obj, int thread_id)
{
	int cpu;
	int anchor_cpu;
	struct paygo_entry *entry;
	cpu = get_cpu();

	anchor_cpu = unrecord_anchor(thread_id, obj);

	// local operation
	if (likely(cpu == anchor_cpu)) {
		cpu_ops_unref_local[cpu] += 1;
		entry = find_hash(obj);
		// All unref operations are called after the ref operation is called.
		// Therefore, there should never be a situation where there is no entry when doing an unref.
		if (!entry) {
			pr_info("paygo_dec: NULL return ERR!!!\n");
			put_cpu();
			return 0;
		}
		entry->local_counter -= 1;
		entry->y -= 1;
	}
	// global operation
	else {
		cpu_ops_unref_other[cpu] += 1;
		dec_other_entry(obj, anchor_cpu);
	}
	put_cpu();
	return 0;
}
EXPORT_SYMBOL(paygo_dec);

bool paygo_read(void *obj)
{
	int mycpu;
	int cur_cpu;
	unsigned long hash;
	struct paygo *p;
	struct overflow *ovfl;
	struct list_head *pos, *n;
	struct paygo_entry *entry;

	mycpu = get_cpu();
	hash = hash_function(obj);

	for_each_possible_cpu(cur_cpu) {
		p = per_cpu(paygo_table_ptr, cur_cpu);
		// self-checking
		if (unlikely(mycpu == cur_cpu)) {
			entry = find_hash(obj);
			if (entry) {
				// There can be a dec_other operation.
				// Therefore, we need to check total count at this time.
				if (entry->local_counter +
					    atomic_read(
						    &(entry->anchor_counter)) >
				    0) {
					put_cpu();
					return false;
				}
			}
			continue;
		}
		// check other cpu
		else {
			// Unlike dec_other, in paygo_read there is no 100% certainty
			// that there is an entry in the hashtable (or in the overflow list).
			// Therefore, we must first prevent the hashtable's owner from removing a entry from the overflow list
			// and adding it to the hashtable. (this step is done in find_hash)
			ovfl = &p->overflow_lists[hash];
			spin_lock(&ovfl->lock);

			entry = &p->entries[hash];
			if (entry->obj == obj) {
				if (entry->local_counter +
					    atomic_read(
						    &(entry->anchor_counter)) >
				    0) {
					spin_unlock(&ovfl->lock);
					put_cpu();
					return false;
				}
			} else {
				list_for_each_safe(pos, n, &ovfl->head) {
					struct paygo_entry *ovfl_entry =
						list_entry(pos,
							   struct paygo_entry,
							   list);
					if (ovfl_entry->obj == obj) {
						if (ovfl_entry->local_counter +
							    atomic_read(&(
								    ovfl_entry
									    ->anchor_counter)) >
						    0) {
							spin_unlock(
								&ovfl->lock);
							put_cpu();
							return false;
						}
					}
				}
			}
			spin_unlock(&ovfl->lock);
			continue;
		}
	}

	put_cpu();
	return true;
}
EXPORT_SYMBOL(paygo_read);

static void record_anchor(int thread_id, int cpu, void *obj)
{
	struct anchor_info *info;
	info = kmalloc(sizeof(struct anchor_info), GFP_KERNEL);
	if (!info) {
		pr_err("Failed to allocate memory for anchor_info\n");
		return;
	}
	info->cpu = cpu;
	info->obj = obj;
	list_add_tail(&info->list, &current->anchor_info_list);
}

static int unrecord_anchor(int thread_id, void *obj)
{
	struct anchor_info *info;
	int cpu = -1;

	list_for_each_entry_reverse(
		info, &current->anchor_info_list, list) {
		if (info->obj == obj) {
			cpu = info->cpu;
			list_del(&info->list);
			kfree(info);
			break;
		}
	}

	// Since all of the unref operations are always followed by ref operations,
	// there is no situation where anchor information list is empty.
	if (cpu == -1) {
		pr_err("Failed to find anchor_info with given obj\n");
	}

	return cpu;
}

static void dec_other_entry(void *obj, int cpu)
{
	unsigned long hash;
	struct overflow *ovfl;
	struct list_head *pos, *n;
	struct paygo *p;

	hash = hash_function(obj);
	p = per_cpu(paygo_table_ptr, cpu);

// If the entry we are looking for does not exist in the overflow list, we should try again
// because the owner of the hash table may have moved the entry from that overflow list to the hashtable.
retry:
	if (p->entries[hash].obj == obj) {
		atomic_dec(&p->entries[hash].anchor_counter);
	} else {
		ovfl = &p->overflow_lists[hash];
		spin_lock(&ovfl->lock);
		list_for_each_safe(pos, n, &ovfl->head) {
			struct paygo_entry *ovfl_entry =
				list_entry(pos, struct paygo_entry, list);
			if (likely(ovfl_entry->obj == obj)) {
				atomic_dec(&ovfl_entry->anchor_counter);
				spin_unlock(&ovfl->lock);
				return;
			}
		}
		spin_unlock(&ovfl->lock);
		goto retry;
	}
}

void traverse_paygo(void)
{
	struct paygo *p;
	struct paygo_entry *entry;
	struct list_head *cur;
	int i;
	int cpu;
	int ovfl_length;

	entry = NULL;

	for_each_possible_cpu(cpu) {
		p = per_cpu(paygo_table_ptr, cpu);
		printk(KERN_INFO "CPU %d:\n", cpu);

		for (i = 0; i < TABLESIZE; i++) {
			ovfl_length = 1;
			entry = &p->entries[i];
			if (entry->obj) {
				printk(KERN_INFO
				       "  Entry %d: obj=%p, local_counter=%d, anchor_counter=%d total_count=%d (x=%d y=%d)\n",
				       i, entry->obj, entry->local_counter,
				       atomic_read(&entry->anchor_counter),
				       entry->local_counter +
					       atomic_read(
						       &entry->anchor_counter),
				       entry->x, entry->y);
			} else {
				printk(KERN_INFO
				       "  Entry %d: obj=%p, local_counter=%d, anchor_counter=%d total_count=%d (x=%d y=%d)\n",
				       i, entry->obj, entry->local_counter,
				       atomic_read(&entry->anchor_counter),
				       entry->local_counter +
					       atomic_read(
						       &entry->anchor_counter),
				       entry->x, entry->y);
			}
			list_for_each(cur, &p->overflow_lists[i].head) {
				entry = list_entry(cur, struct paygo_entry,
						   list);
				printk(KERN_INFO
				       "  \tOverflow Entry %d-%d: obj=%p, local_counter=%d, anchor_counter=%d total_count=%d (x=%d y=%d)\n",
				       i, ovfl_length, entry->obj,
				       entry->local_counter,
				       atomic_read(&entry->anchor_counter),
				       entry->local_counter +
					       atomic_read(
						       &entry->anchor_counter),
				       entry->x, entry->y);
				ovfl_length++;
			}
		}
	}
	pr_info("NOBJS: %d\n", NOBJS);
	for_each_possible_cpu(cpu) {
		pr_info("CPU[%d]: ref(%d) unref_local(%d) unref_other(%d)\n",
			cpu, cpu_ops_ref[cpu], cpu_ops_unref_local[cpu],
			cpu_ops_unref_other[cpu]);
	}
	for (i = 0; i < NTHREAD; ++i) {
		pr_info("THREAD%d did %d jobs\n", i, thread_ops[i]);
	}
}
EXPORT_SYMBOL(traverse_paygo);

static int thread_fn(void *data)
{
	int i;
	struct thread_data td;
	i = 0;
	td = *(struct thread_data *)data;
	while (!kthread_should_stop()) {
		paygo_inc(objs[i % NOBJS], td.thread_id);
		paygo_inc(objs[(i+1) % NOBJS], td.thread_id);
		paygo_inc(objs[(i+2) % NOBJS], td.thread_id);
		msleep(0);
		paygo_dec(objs[(i+1) % NOBJS], td.thread_id);
		//pr_info("%d\n", paygo_read(objs[(i+1) % NOBJS]));
		paygo_dec(objs[i % NOBJS], td.thread_id);
		paygo_dec(objs[(i+2) % NOBJS], td.thread_id);

		i++;
	}
	thread_ops[td.thread_id] = i;
	if(list_empty(&current->anchor_info_list)) {
		pr_info("thread[%d]'s anchor info list is empty!\n", td.thread_id);
	}
	pr_info("thread end!\n");
	atomic_inc(&thread_done);
	return 0;
}

MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
