#include "paygo.h"


static int thread_fn(void *data);
static struct task_struct *thread;

static int __init start_module(void)
{

	pr_info("paygo module loades!\n");
	pr_info("entry size = %lu\n", sizeof(struct paygo_entry));
	init_paygo_table();	
  
	thread = kthread_run(thread_fn, NULL, "my_kthread");

	return 0;
}

static void __exit end_module(void)
{
  kthread_stop(thread);
	msleep(1000);	
	
	traverse_paygo();
	pr_info("paygo module removed!\n");
}

void init_paygo_table(void)
{
	int cpu;
	struct paygo *p;

	for_each_possible_cpu(cpu) {
		p = kzalloc(sizeof(struct paygo), GFP_KERNEL);
		if (!p) {
			pr_err("Failed to allocate paygo table for CPU %d\n", cpu);
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

  return hash % TABLESIZE;
}

int push_hash(void *obj)
{
	unsigned int hash;
	struct paygo_entry *entry;
	struct overflow *ovfl;
	struct paygo *p = get_cpu_var(paygo_table_ptr);

	hash = hash_function(obj);

	entry = &p->entries[hash];

	if (entry->obj == NULL) {
		{
			entry->obj = obj;
			entry->local_counter = 0;
			atomic_set(&entry->anchor_counter, 0);
		}
		put_cpu_var(paygo_table_ptr);

		return 0;
	} else {
		struct paygo_entry *new_entry;
		ovfl = &p->overflow_lists[hash];
		spin_lock(&ovfl->lock);

		new_entry = kzalloc(sizeof(struct paygo_entry), GFP_ATOMIC);
		if (!new_entry) {
				spin_unlock(&ovfl->lock);
				put_cpu_var(paygo_table_ptr);
				return -ENOMEM;
		}

		{
			new_entry->obj = obj;
			new_entry->local_counter = 0;
			atomic_set(&new_entry->anchor_counter, 0);
		}
		list_add(&new_entry->list, &ovfl->head);
		spin_unlock(&ovfl->lock);
		put_cpu_var(paygo_table_ptr);
		return 0;
	}
}

struct paygo_entry* find_hash(void *obj)
{
	unsigned int hash;
	struct paygo_entry *entry;
	struct overflow *ovfl;
	struct list_head *pos, *n;
	struct paygo *p = this_cpu_ptr(paygo_table_ptr);

	hash = hash_function(obj);

	entry = &p->entries[hash];

	if (entry->obj == obj) {
		return entry;
	} else {
		ovfl = &p->overflow_lists[hash];
		spin_lock(&ovfl->lock);

		list_for_each_safe(pos, n, &ovfl->head) {
			struct paygo_entry *ovfl_entry = list_entry(pos, struct paygo_entry, list);

			if (ovfl_entry->obj == obj) {
				spin_unlock(&ovfl->lock);
				return ovfl_entry;
			}
		}

		spin_unlock(&ovfl->lock);
	}

    return NULL;
}


void dec_other_entry(void *obj, int cpu) {
	unsigned int hash;
	struct overflow *ovfl;
	struct list_head *pos, *n;
	struct paygo *p;

	hash = hash_function(obj);

	preempt_disable(); 
	p = per_cpu_ptr(paygo_table_ptr, cpu);

	if (p->entries[hash].obj == obj) {
		atomic_dec(&p->entries[hash].anchor_counter);
	} else {
		ovfl = &p->overflow_lists[hash];
		spin_lock(&ovfl->lock);
		
		list_for_each_safe(pos, n, &ovfl->head) {
			struct paygo_entry *ovfl_entry = list_entry(pos, struct paygo_entry, list);

			if (ovfl_entry->obj == obj) {
				atomic_dec(&ovfl_entry->anchor_counter);
				break;
			}
		}

		spin_unlock(&ovfl->lock);
	}

	preempt_enable(); 
}


void traverse_paygo(void) {
	struct paygo *p;
	struct paygo_entry *entry;
	struct list_head *cur;
	int i;
	int cpu;
	entry = NULL;

	for_each_possible_cpu(cpu) {
		p = per_cpu(paygo_table_ptr, cpu);
		printk(KERN_INFO "CPU %d:\n", cpu);

		for (i = 0; i < TABLESIZE; i++) {
			entry = &p->entries[i];						
			if (entry->obj) {
					printk(KERN_INFO "  Entry %d: obj=%p, local_counter=%d, anchor_counter=%d\n", i, entry->obj, entry->local_counter, atomic_read(&entry->anchor_counter));
			}
			else {
					printk(KERN_INFO "  Entry %d: obj=NULL, local_counter=%d, anchor_counter=%d\n", i , entry->local_counter, atomic_read(&entry->anchor_counter));
			}
			list_for_each(cur, &p->overflow_lists[i].head) {
					entry = list_entry(cur, struct paygo_entry, list);
					printk(KERN_INFO "  Overflow entry: obj=%p, local_counter=%d, anchor_counter=%d\n", entry->obj, entry->local_counter, atomic_read(&entry->anchor_counter));
			}	
		}
	}
}


static int thread_fn(void *data)
{
	int i;
	struct paygo_entry *entry;
	i = 0;
	while (!kthread_should_stop()) {
		entry = kmalloc(sizeof(struct paygo_entry), GFP_KERNEL);
		if (entry == NULL) {
			printk(KERN_ERR "Failed to allocate paygo_entry\n");
			continue;
		}
		entry->obj = entry;  
		entry->local_counter = i++;
		atomic_set(&entry->anchor_counter, i);

		push_hash(entry);
		msleep(5);  
	}
	pr_info("thread end!\n");
	return 0;
}





MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
