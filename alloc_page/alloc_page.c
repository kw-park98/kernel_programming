/**
 * alloc_page.c - allocate pages
 *
 * Allocate pages
 * - struct page *alloc_pages(gfp_mask, order)
 * Deallocate pages
 * - __free_pages(page, order)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/page_ref.h>

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

MODULE_AUTHOR("Hyeonmin Lee");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
