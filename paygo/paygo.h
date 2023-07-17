#ifndef __PAYGO_H__
#define __PAYGO_H__

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

#define TABLESIZE 16
#define HASHSHIFT 11


// for thread

struct anchor_info {
	int cpu;
	struct list_head list;
};

struct thread_data {
	int thread_id;
	struct list_head anchor_info_list;		
};

#define NTHREAD 10
static int thread_fn(void *data);
static struct task_struct *threads[NTHREAD];

// This is the data structure that will be used in place of tast struct.
static struct thread_data thread_datas[NTHREAD];

static int thread_ops[NTHREAD];

#define NOBJS 20
static void** objs;
//

/* Functions

-------------------------------------------------------
These functions are used for setting up.

1. init_paygo_table
2. hash_function
-------------------------------------------------------
These functions are used in hashtable.

3. push_hash 
4. find_hash
-------------------------------------------------------
These functions are APIs for reference counting.

5. paygo_ref
6. paygo_unref (~ing until 7/16 (maybe) )

7. paygo_read (not yet)
-------------------------------------------------------
test function

7. traverse_paygo
-------------------------------------------------------
*/


struct __attribute__((aligned(64))) paygo_entry {
	void *obj;
	int local_counter;
	atomic_t anchor_counter;
	struct list_head list;
};

struct overflow {
	spinlock_t lock;
	struct list_head head;
};

struct paygo {
	struct paygo_entry entries[TABLESIZE];
	struct overflow overflow_lists[TABLESIZE];
};

DEFINE_PER_CPU(struct paygo *, paygo_table_ptr);

/**
 * init_paygo_table
 *
 * allocate memory for hashtable and initialize the table
 */
void init_paygo_table(void);

/**
 * hash_function
 *
 * @obj: pointer of the object
 *
 * Return: index of the object in the PCP-hashtable
 */
unsigned long hash_function(const void *obj);

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
int push_hash(void *obj);

/**
 * find_hash
 *
 * @obj: pointer of the object
 *
 * Return: pointer of the paygo entry from the per-cpu hashtable.
 *
 * find paygo entry from the per-cpu hashtable.
 *
 */
struct paygo_entry *find_hash(void *obj);

/**
 * paygo_ref
 *
 * @obj: pointer of the object
 *
 * Return: 0 for success
 *
 * increment reference count of the object.
 *
 */
int paygo_ref(void *obj, int thread_id);

/**
 * paygo_unref
 *
 * @obj: pointer of the object
 *
 * Return: 0 for success
 *
 * decrement reference count of the object.
 *
 */
int paygo_unref(void *obj, int thread_id);

/**
 * traverse_paygo
 *
 * Context: This function must run alone. 
 *
 * Traverse hashtables and print about all of the per-cpu hashtable's entries.
 *
 */
void traverse_paygo(void);

#endif // __PAYGO_H__
