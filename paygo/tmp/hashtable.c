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
int push_entry(struct paygo_entry *new_entry);

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
	unsigned char *key = (unsigned char*)obj;
	size_t len = sizeof(obj);
	for(hash = i = 0; i < len; ++i)
	{
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash % TSIZE;
}


///**
// * push_hash
// *
// * PREEMPTION DISABLE BEFORE CALL (cpu = get_cpu())
// * 
// * @cpu: cpu of the current task
// * @entry: pointer of the entry which will be pushed
// *
// */
//int push_hash(int cpu, struct paygo_entry *entry)
//{
//	unsigned int hindex;
//	struct paygo_hashtable *htable;
//	hindex = hash_function(entry->obj);
//	
//	htable = &per_cpu(pcp_hashtable, cpu);	
//	if(htable->entries[hindex].obj != NULL) {
//		struct paygo_entry *overflow_entry;
//		overflow_entry = kmalloc(sizeof(struct paygo_entry), GFP_KERNEL);
//		*overflow_entry = htable->entries[hindex];	
//		put_entry(&overflow, overflow_entry);
//	}	
//	htable->entries[hindex] = *entry; 
//	return 0;	
//}

/**
 * find_hash
 *
 * PREEMPTION DISABLE BEFORE CALL (cpu = get_cpu())
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
	if(htable->entries[hindex].obj == obj) {
		entry = &(htable->entries[hindex]);
	}
	if(entry)
		return entry;

	overflow_entry = &(htable->entries[hindex]);
	// if htable doesn't have the entry
	// then we need to find the overflow list.
	entry = get_entry(obj);	
	if(entry) {
		if(overflow_entry->obj != NULL) {
			put_entry(&overflow, overflow_entry);
		}
		htable->entries[hindex] = *entry;
		kfree(entry);
		entry = &(htable->entries[hindex]);
	}			
	return entry;
}


MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
