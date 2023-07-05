#include <linux/module.h>     /* Needed by all modules */
#include <linux/kernel.h>     /* Needed for KERN_INFO */
#include <linux/init.h>       /* Needed for the macros */
#include <linux/percpu.h>

///< The license type -- this affects runtime behavior
MODULE_LICENSE("GPL");

///< The author -- visible when you use modinfo
MODULE_AUTHOR("Kunwook Park");

///< The description -- see modinfo
MODULE_DESCRIPTION("PERCPU MACRO Test");

///< The version of the module
MODULE_VERSION("0.1");

//DEFINE_PER_CPU(TYPE, NAME)
DEFINE_PER_CPU(int, my_percpu);

int cpu;
static int __init start_module(void)
{
  printk(KERN_INFO "percpu_test(MACRO): module loading\n");

	for_each_possible_cpu(cpu)
	{
		per_cpu(my_percpu, cpu) = 0;		
	} 
	per_cpu(my_percpu, 0) = 5;

	printk(KERN_INFO "percpu_test(MACRO): module loaded\n");

  return 0;
}

static void __exit end_module(void)
{
	printk(KERN_INFO "percpu_test(MACRO): module unloading\n");
	
  for_each_possible_cpu(cpu)
  {
    printk(KERN_INFO "percpu_test(MACRO): my_percpu[%d] = %d\n", cpu, per_cpu(my_percpu, cpu));
  }
	printk(KERN_INFO "percpu_test(MACRO): module unloaded\n");
}

module_init(start_module);
module_exit(end_module);

