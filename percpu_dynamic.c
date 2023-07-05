#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/init.h> /* Needed for the macros */
#include <linux/percpu.h>

///< The license type -- this affects runtime behavior
MODULE_LICENSE("GPL");

///< The author -- visible when you use modinfo
MODULE_AUTHOR("Kunwook Park");

///< The description -- see modinfo
MODULE_DESCRIPTION("PERCPU DYNAMIC Test");

///< The version of the module
MODULE_VERSION("0.1");

int cpu;
int __percpu *my_percpu;

static int __init start_module(void)
{
	printk(KERN_INFO "percpu_test(DYNAMIC): module loading\n");

	my_percpu = alloc_percpu(int);
	if (!my_percpu) {
		printk(KERN_ERR
		       "percpu_test: failed to allocate percpu variable\n");
		return -ENOMEM;
	}

	for_each_possible_cpu(cpu) {
		*per_cpu_ptr(my_percpu, cpu) = 0;
	}
	*per_cpu_ptr(my_percpu, 0) = 5;

	printk(KERN_INFO "percpu_test(DYNAMIC): module loaded\n");

	return 0;
}

static void __exit end_module(void)
{
	printk(KERN_INFO "percpu_test(DYNAMIC): module unloading\n");

	for_each_possible_cpu(cpu) {
		printk(KERN_INFO "percpu_test: my_percpu[%d] = %d\n", cpu,
		       *per_cpu_ptr(my_percpu, cpu));
	}
	free_percpu(my_percpu);

	printk(KERN_INFO "percpu_test(DYNAMIC): module unloaded\n");
}

module_init(start_module);
module_exit(end_module);
