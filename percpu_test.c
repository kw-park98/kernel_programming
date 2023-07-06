#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/init.h> /* Needed for the macros */
#include <linux/percpu.h>
#include <linux/percpu-defs.h>
#include <linux/sched.h>


/* DESCRIPTION

1) allocate

 There are two ways to allocate percpu variable.

	1. To create a per-CPU variable at compile time.
		=> use MACRO

	2. To create a per-CPU variable dynamicaly.
		=> use FUNCTION
			void __percpu *alloc_percpu(type)
			void __percpu *__alloc_percpu(size_t size, size_t align)

2) use

	1. get_cpu_var, put_cpu_var
	2. get_cpu_ptr, put_cpu_ptr

		=> contain preemption disable(O)


	3. per_cpu(var, cpu)
	4. per_cpu_ptr(ptr, cpu)

		=> contain preemption disalbe(X)
*/

int __percpu *my_percpu_dynamic;
DEFINE_PER_CPU(int, my_percpu_macro);			//allocate percpu<1>

int i;
int cpu;
int *ptr;

static int __init start_module(void)
{
  printk(KERN_INFO "percpu_test: module loaded\n");

	my_percpu_dynamic = alloc_percpu(int);	//allocate percpu<2>

	cpu = get_cpu();  //preemption enable
	per_cpu(my_percpu_macro, cpu) = 10000000;	
	per_cpu(*my_percpu_dynamic, cpu) = 10000000;
	(*per_cpu_ptr(my_percpu_dynamic, cpu)) *= 2;
	put_cpu();				//preemption disable

	for(i=0; i<1000000; i++) {
		ptr = get_cpu_ptr(my_percpu_dynamic);	//preemption enable
		*ptr += 1;
		put_cpu_ptr(my_percpu_dynamic);				//preemption disable
	}
  return 0;
}

static void __exit end_module(void)
{
	for_each_possible_cpu(cpu) {
		printk(KERN_INFO "percpu_test: cpu[%d] => %d(macro), %d(dynamic)", cpu, per_cpu(my_percpu_macro, cpu), *per_cpu_ptr(my_percpu_dynamic, cpu));
	}
	free_percpu(my_percpu_dynamic);
	printk(KERN_INFO "percpu_test: module unloaded\n");
}



///< The author -- visible when you use modinfo
MODULE_AUTHOR("Kunwook Park");

///< The license type -- this affects runtime behavior
MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
