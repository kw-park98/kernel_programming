#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/slab.h>

#include "ds_paygo_entry.h"
#include "ds_overflow.h"

DEFINE_SPINLOCK(hlist_lock);
DEFINE_HASHTABLE(obj_to_list_map, 8);

struct paygo_overflow_list *find_overflow_list_for_obj(void *obj);

// APIs for other part
void put_entry(struct paygo_entry *entry);
struct paygo_entry *get_entry(void *obj);
void traverse(void *obj);

static int __init start_module(void)
{
	pr_info("overflow module loaded\n");
	pr_info("size of paygo_entry: %lu\n", sizeof(struct paygo_entry)); 
	return 0;
}

static void __exit end_module(void)
{
	pr_info("overflow module removed\n");
}

struct paygo_overflow_list *find_overflow_list_for_obj(void *obj)
{
	unsigned long flags;
	struct perobj_overflow_list *mapping;
	spin_lock_irqsave(&hlist_lock, flags);
	hash_for_each_possible(obj_to_list_map, mapping, hnode, (unsigned long)obj) {
		if (mapping->obj == obj) {
			spin_unlock_irqrestore(&hlist_lock, flags);
			return &mapping->overflow;
		}
	}
	
	mapping = kmalloc(sizeof(struct perobj_overflow_list), GFP_KERNEL);
	if (mapping == NULL)
		return NULL;
	
	mapping->obj = obj;
	spin_lock_init(&mapping->overflow.lock);
	INIT_LIST_HEAD(&mapping->overflow.head);
	
	hash_add(obj_to_list_map, &mapping->hnode, (unsigned long)obj);
	spin_unlock_irqrestore(&hlist_lock, flags);
	return &mapping->overflow;
}


/*
 * put_entry
 *
 * @overflow_list: overflow list of the entry's obj
 * @entry: pointer of the entry 
 *
 * Context: Preemtion disabled context
 */
void put_entry(struct paygo_entry *entry) 
{
	unsigned long flags;
	struct paygo_entry *new_entry;
  struct paygo_overflow_list *overflow_list;

  overflow_list = find_overflow_list_for_obj(entry->obj);
	new_entry = kmalloc(sizeof(struct paygo_entry), GFP_KERNEL);
	*new_entry = *entry;

	spin_lock_irqsave(&overflow_list->lock, flags);
	list_add(&entry->list, &overflow_list->head);
	spin_unlock_irqrestore(&overflow_list->lock, flags);
}
EXPORT_SYMBOL(put_entry);


/**
 * get_entry
 *
 * @overflow_list: overflow list of the obj
 * @obj: pointer of the object
 * 
 * Context: Preemtion disabled context
 *
 * Return: pointer of the entry which has the argument's obj. if there is not in list, then NULL.
 */
struct paygo_entry *get_entry(void *obj)
{
	unsigned long flags;
	struct paygo_entry *entry;
	struct paygo_entry *ret_entry;
	struct paygo_overflow_list *overflow_list;
	ret_entry = NULL;
	overflow_list = find_overflow_list_for_obj(obj);
	spin_lock_irqsave(&overflow_list->lock, flags);
	list_for_each_entry(entry, &overflow_list->head, list) {
		if(entry->obj == obj && entry->cpu == smp_processor_id()) {
			list_del(&entry->list); 
			ret_entry = entry;
			break;
		}
	}
	spin_unlock_irqrestore(&overflow_list->lock, flags);
	return ret_entry;
}
EXPORT_SYMBOL(get_entry);


/**
 * traverse
 *
 * @obj: pointer of the object
 * 
 * traverse and print about the overflow list whose obj is the same with the argument.
 */
void traverse(void *obj)
{
	int count;
	struct paygo_entry *entry;
	struct paygo_overflow_list *overflow_list;
	
	count = 0;
	overflow_list = find_overflow_list_for_obj(obj);		
	list_for_each_entry(entry, &overflow_list->head, list) {
		count += 1;
		pr_info("CPU: %d, obj: %p, local: %d, anchor: %d\n", entry->cpu, entry->obj, entry->local_count, entry->anchor_count);
	}
	
	pr_info("total entries in the list: %d\n", count);
}
EXPORT_SYMBOL(traverse);

MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
