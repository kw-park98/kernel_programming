#include "paygo.h"


static void record_anchor(int cpu, int thread_id);
static int unrecord_anchor(int thread_id);

void dec_other_entry(void *obj, int cpu);

static int __init start_module(void)
{
	int i;
	pr_info("paygo module loades!\n");
	pr_info("entry size = %lu\n", sizeof(struct paygo_entry));
	objs= kzalloc(sizeof(void*) * NOBJS, GFP_KERNEL); 
	for(i=0; i<NOBJS; i++) {
		objs[i] = kzalloc(sizeof(void*), GFP_KERNEL);
	}
	for(i=0; i<NTHREAD; i++) {
		thread_datas[i].thread_id = i;
	}

	init_paygo_table();

	for(i=0; i<NTHREAD; i++) {
		threads[i] = kthread_run(thread_fn, &thread_datas[i], "my_kthread%d", i);	
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
	for(i=0; i<NTHREAD; i++) {
		if(threads[i]) {
			kthread_stop(threads[i]);
			threads[i] = NULL;
		}
	} 
	msleep(2000);
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

unsigned long hash_function(const void *obj)
{
	unsigned long ret;
	unsigned long hash;
	hash = hash_64((unsigned long)obj, HASHSHIFT);
	ret = hash % TABLESIZE;
	return ret;
}

int push_hash(void *obj)
{
	unsigned long hash;
	struct paygo_entry *entry;
	struct overflow *ovfl;
	//struct paygo *p = get_cpu_var(paygo_table_ptr);
	struct paygo *p = per_cpu(paygo_table_ptr, smp_processor_id());
	//struct paygo *p = this_cpu_ptr(paygo_table_ptr);

	hash = hash_function(obj);
	entry = &p->entries[hash];

	if (entry->obj == NULL) {
		{
			entry->obj = obj;
			entry->local_counter = 1;
			atomic_set(&entry->anchor_counter, 0);
		}
		//put_cpu_var(paygo_table_ptr);
		return 0;
	} else {
		struct paygo_entry *new_entry;
		ovfl = &p->overflow_lists[hash];
    spin_lock(&ovfl->lock);

		new_entry = kzalloc(sizeof(struct paygo_entry), GFP_ATOMIC);
		if (!new_entry) {
			spin_unlock(&ovfl->lock);
			//put_cpu_var(paygo_table_ptr);
			return -ENOMEM;
		}

		{
			new_entry->obj = obj;
			new_entry->local_counter = 1;
			atomic_set(&new_entry->anchor_counter, 0);
		}
		list_add(&new_entry->list, &ovfl->head);
		spin_unlock(&ovfl->lock);
		//put_cpu_var(paygo_table_ptr);
		return 0;
	}
}

struct paygo_entry *find_hash(void *obj)
{
	unsigned long hash;
	struct paygo_entry *entry;
	struct overflow *ovfl;
	struct list_head *pos, *n;
	//struct paygo *p = this_cpu_ptr(paygo_table_ptr);
	struct paygo *p = per_cpu(paygo_table_ptr, smp_processor_id());

	hash = hash_function(obj);

	entry = &p->entries[hash];

	if (entry->obj == obj) {
		return entry;
	} else {
		ovfl = &p->overflow_lists[hash];
    spin_lock(&ovfl->lock);

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

int paygo_ref(void *obj, int thread_id)
{
	int ret;
	int cpu;
	struct paygo_entry *entry;
	cpu = get_cpu();
	entry = find_hash(obj);
	// if there is an entry!
	if(entry) {
		entry->local_counter += 1;
		record_anchor(cpu, thread_id);
		put_cpu();
		return 0;
	}

	// if there isn't
	ret = push_hash(obj);
	record_anchor(cpu, thread_id);
	put_cpu();
	return ret;
}

int paygo_unref(void *obj, int thread_id)
{
	int cpu;
	int anchor_cpu;
	struct paygo_entry *entry;
	cpu = get_cpu();
	anchor_cpu = unrecord_anchor(thread_id);
	if(cpu == anchor_cpu) {
		entry = find_hash(obj);
		entry->local_counter -= 1;				
	} else {
		dec_other_entry(obj, anchor_cpu);
	}
	put_cpu();
	return 0;
}

void dec_other_entry(void *obj, int cpu)
{
	unsigned long hash;
	struct overflow *ovfl;
	struct list_head *pos, *n;
	struct paygo *p;

	hash = hash_function(obj);
	p = per_cpu(paygo_table_ptr, cpu);

	if (p->entries[hash].obj == obj) {
		atomic_dec(&p->entries[hash].anchor_counter);
	} else {
		ovfl = &p->overflow_lists[hash];
		spin_lock(&ovfl->lock);
		list_for_each_safe(pos, n, &ovfl->head) {
			struct paygo_entry *ovfl_entry =
				list_entry(pos, struct paygo_entry, list);			
			if (ovfl_entry->obj == obj) {
				atomic_dec(&ovfl_entry->anchor_counter);
				break;
			}
		}
		spin_unlock(&ovfl->lock);
	}
}

void traverse_paygo(void)
{
	struct paygo *p;
	struct paygo_entry *entry;
	struct list_head *cur;
	int i;
	int cpu;
	int cpu_ops[128];
	int last_cpu;
	int dif_sum;

	entry = NULL;

	for_each_possible_cpu(cpu) {
		last_cpu = cpu;
		cpu_ops[last_cpu] = 0;
		p = per_cpu(paygo_table_ptr, cpu);
		printk(KERN_INFO "CPU %d:\n", cpu);

		for (i = 0; i < TABLESIZE; i++) {
			entry = &p->entries[i];
			if (entry->obj) {
				printk(KERN_INFO
				       "  Entry %d: obj=%p, local_counter=%d, anchor_counter=%d\n",
				       i, entry->obj, entry->local_counter,
				       atomic_read(&entry->anchor_counter));
			} else {
				printk(KERN_INFO
				       "  Entry %d: obj=NULL, local_counter=%d, anchor_counter=%d\n",
				       i, entry->local_counter,
				       atomic_read(&entry->anchor_counter));
			}
			{
				cpu_ops[last_cpu] += entry->local_counter;
				cpu_ops[last_cpu] += atomic_read(&entry->anchor_counter);
			}
			list_for_each(cur, &p->overflow_lists[i].head) {
				entry = list_entry(cur, struct paygo_entry,
						   list);
				printk(KERN_INFO
				       "  Overflow entry: obj=%p, local_counter=%d, anchor_counter=%d\n",
				       entry->obj, entry->local_counter,
				       atomic_read(&entry->anchor_counter));
				{
					cpu_ops[last_cpu] += entry->local_counter;
					cpu_ops[last_cpu] += atomic_read(&entry->anchor_counter);
				}
			}
		}
	}
	dif_sum = 0;
	cpu_ops[last_cpu+1] = -1;
	for(i=0; cpu_ops[i] != -1; ++i) {
		pr_info("CPU[%d]'s operations = %d\n", i, cpu_ops[i]);
		dif_sum += cpu_ops[i];
	}
	for(i=0; i<NTHREAD; ++i) {
		pr_info("THREAD%d did %d jobs\n", i, thread_ops[i]);
		dif_sum -= thread_ops[i];
	}
	
	pr_info("differences between the sum of CPUs and THREADs = %d\n", dif_sum);

}

static int thread_fn(void *data)
{
	int i;
	struct thread_data td;
	i = 0;
	td = *(struct thread_data *)data;
	INIT_LIST_HEAD(&thread_datas[td.thread_id].anchor_info_list);
	while (!kthread_should_stop()) {
		paygo_ref(objs[i % NOBJS], td.thread_id);
		msleep(0);
		paygo_unref(objs[i % NOBJS], td.thread_id);
		i++;
		msleep(0);
	}
	pr_info("thread%d did %d jobs\n", td.thread_id, i);
	thread_ops[td.thread_id] = i;
	pr_info("thread end!\n");
	return 0;
}

static void record_anchor(int cpu, int thread_id)
{
	struct anchor_info *info;
	info = kmalloc(sizeof(struct anchor_info), GFP_KERNEL);
	if (!info) {
		pr_err("Failed to allocate memory for anchor_info\n");
		return;
  }
	info->cpu = cpu;
	list_add_tail(&info->list, &thread_datas[thread_id].anchor_info_list);
}

static int unrecord_anchor(int thread_id)
{
	int cpu;
	// Since all of the unref operations are always followed by ref operations, 
	// there is no situation where anchor information list is empty.
	struct anchor_info *last_info;
	last_info = list_last_entry(&thread_datas[thread_id].anchor_info_list, struct anchor_info, list);
	
	cpu = last_info->cpu;
	list_del(&last_info->list);
	kfree(last_info);

	return cpu;	
}

MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
