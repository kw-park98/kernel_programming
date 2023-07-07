/**
 * spinlock_test.c = simple spinlock test
 *
 * 
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(sl_static);
static spinlock_t sl_dynamic;

static void example_spinlock_static(void)
{
	unsigned long flags;

	spin_lock_irqsave(&sl_static, flags);
	printk(KERN_INFO "spinlock_test: locked static spinlock\n");

	/* Do someting here */

	spin_unlock_irqrestore(&sl_spinlock, flags);
	printk(KERN_INFO "spinlock_test: unlocked static spinlock\n");
}

static void example_spinlock_dynamic(void)
{
	unsigned long flags;

	spin_lock_init(&sl_dynamic);
	spin_lock_irqsave(&sl_dynamic, flags);
	printk(KERN_INFO "spinlock_test: locked dynamic spinlock\n");

	/* Do something here */

	spin_unlock_irqrestore(&sl_dynamic, flags);
	printk(KERN_INFO "spinlock_test: unlocked dynamic spinlock\n");
}
	

static int __init start_module(void)
{
	printk(KERN_INFO "spinlock_test: example spinlock started\n");

	example_spinlock_static();
	example_spinlock_dynamic();

	return 0;
}

static void __exit end_module(void)
{
	printk(KERN_INFO "spinlock_test: example spinlock exit\n");
}

MODULE_AUTHOR("Jeonghoon Lee");
MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
