#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#include "ds_paygo_entry.h"
#include "ds_overflow.h"


#define NKTHREAD 6

extern void put_entry(struct paygo_entry *entry);
extern struct paygo_entry *get_entry(void *obj);

extern void traverse(void *obj);

//thread function
void *ptr;
//static int get_fn(void *data);
static int put_fn(void *data);

struct kthread_data {
  int kthread_id;
  int start;
  int end;
};
static struct task_struct *kthreads[NKTHREAD];

static int __init start_module(void) {
  int i;
  struct kthread_data *data;
  pr_info("overflow test module loaded!\n");
  ptr = kmalloc(sizeof(int), GFP_KERNEL);
  for(i = 0; i < NKTHREAD; i++) {
    data = kmalloc(sizeof(struct kthread_data), GFP_KERNEL);
    {
      data->kthread_id = i;
      data->start = i*30;
      data->end = (i+1)*30;
    }
    kthreads[i] = kthread_run(put_fn, (void *)data, "kthread%d", i);
    if(IS_ERR(kthreads[i])){
      printk(KERN_ERR "Failed to create kthread");
      return PTR_ERR(kthreads[i]);
    }
  }
  return 0;
}

static void __exit end_module(void) {
	traverse(ptr);	
	pr_info("overflow test module removed!\n");
}

static int put_fn(void *data)
{
  int i;
  int cpu;
  struct kthread_data *kdata;
  struct paygo_entry *entry;
  i = 0;
  kdata = (struct kthread_data *)data;
  for(i=kdata->start; i<kdata->end; i++) {
    entry = kmalloc(sizeof(struct paygo_entry), GFP_KERNEL);
    cpu = get_cpu();
    {
      entry->obj = ptr;
      entry->local_count = i;
      entry->anchor_count = i;
      entry->cpu = cpu;
    }
    put_entry(entry);
    put_cpu();
    msleep(1);
  }
  pr_info("thread%d ENDED!\n", kdata->kthread_id);
  return 0;
}

MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
