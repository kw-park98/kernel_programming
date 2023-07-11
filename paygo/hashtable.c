#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>

#include <linux/slab.h>

#include "ds_paygo_entry.h"
#include "ds_overflow.h"
#include "ds_hashtable.h"

DEFINE_PER_CPU(struct paygo_hashtable, pcp_hashtable);

unsigned int hash_function(void *obj);
struct paygo_entry *find_hash(int cpu, void *obj);

static int __init start_module(void)
{
	pr_info("hashtable module loades!\n");
	return 0;
}

static void __exit end_module(void)
{
	pr_info("hashtable module removed!\n");
}

/**
 * hash_function
 *
 * @obj: pointer of the object
 *
 * Return: index of the object in the PCP-hashtable 
 */
unsigned int hash_function(void *obj)
{
	int i;
	unsigned int hash;
	unsigned char *key = (unsigned char *)obj;
	size_t len = sizeof(obj);
	for (hash = i = 0; i < len; ++i) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash % TSIZE;
}

/**
 * find_hash
 *
 * Context: Preemtion disabled context
 * 
 * @cpu: cpu of the current task
 * @obj: pointer of the object
 *
 * Return: pointer of the entry whose cpu and obj is the same with the arguments
 */
struct paygo_entry *find_hash(int cpu, void *obj)
{
	unsigned int hindex;
	struct paygo_entry *entry = NULL;
	struct paygo_entry *overflow_entry;
	struct paygo_hashtable *htable;

	htable = &per_cpu(pcp_hashtable, cpu);
	hindex = hash_function(obj);
	if (htable->entries[hindex].obj == obj) {
		entry = &(htable->entries[hindex]);
	}
	if (entry)
		return entry;

	overflow_entry = &(htable->entries[hindex]);
	if (overflow_entry->obj != NULL) {
		put_entry(overflow_entry);
	}
	// if htable doesn't have the entry
	// then we need to find the overflow list.
	entry = get_entry(obj);
	if (entry) {
		htable->entries[hindex] = *entry;
		kfree(entry);
		entry = &(htable->entries[hindex]);
	} else {
		entry = &(htable->entries[hindex]);
		{
			entry->obj = obj;
			entry->cpu = cpu;
			entry->anchor_count = 0;
			entry->local_count = 0;
		}
	}
	return entry;
}
EXPORT_SYMBOL(find_hash);

MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
