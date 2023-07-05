#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/init.h> /* Needed for the macros */
#include <linux/page_ref.h>

///< The license type -- this affects runtime behavior
MODULE_LICENSE("GPL");

///< The author -- visible when you use modinfo
MODULE_AUTHOR("Hyeonmin Lee");

///< The description -- see modinfo
MODULE_DESCRIPTION("Alloc Page Test");

///< The version of the module
MODULE_VERSION("0.1");

struct page *p = NULL;

static int __init start_module(void)
{
	p = alloc_pages(GFP_KERNEL, 0);
	printk(KERN_INFO "Loading module...\n");
	printk(KERN_INFO "[before] refcount: %d\n", page_count(p));
	for (int i = 0; i < 1000; i++) {
		preempt_disable();
		page_ref_inc(p);
		preempt_enable();
	};

	return 0;
}

static void __exit end_module(void)
{
	printk(KERN_INFO "Unloading module...\n");
	printk(KERN_INFO "[after] refcount: %d\n", page_count(p));
	__free_pages(p, 0);
}

module_init(start_module);
module_exit(end_module);
