/**
 * percpu_test.c - percpu variable
 *
 * This module is written to try to allocate
 * and use per-cpu variables.
 *
 * how to allocate percpu var
 * 1. Static allocation: compile time
 *    - DEFINE_PER_CPU(type, name)
 *    
 * 2. Dynamic allocation: runtime
 *    - alloc_percpu(type)
 *    - __alloc_percpu(size, align)
 *
 * how to use percpu var
 * 1. Disable preemption
 *    - get_cpu_var(var), put_cpu_var(var)
 *    - get_cpu_ptr(ptr), put_cpu_ptr(ptr)
 * 2. Not disable preemtion
 *    - per_cpu(var, cpu)
 *    - per_cpu_ptr(ptr, cpu)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/percpu-defs.h>
#include <linux/sched.h>

int __percpu *my_percpu_dynamic;
DEFINE_PER_CPU(int, my_percpu_macro); // static alloc

int i;
int cpu;
int *ptr;

static int __init start_module(void)
{
	printk(KERN_INFO "percpu_test: module loaded\n");

	my_percpu_dynamic = alloc_percpu(int); // dynamic alloc

	cpu = get_cpu(); // preemption disable
	per_cpu(my_percpu_macro, cpu) = 10000000;
	per_cpu(*my_percpu_dynamic, cpu) = 10000000;
	(*per_cpu_ptr(my_percpu_dynamic, cpu)) *= 2;
	put_cpu(); // preemption enable

	for (i = 0; i < 1000000; i++) {
		ptr = get_cpu_ptr(my_percpu_dynamic); // preemption disable
		*ptr += 1;
		put_cpu_ptr(my_percpu_dynamic); // preemption enable
	}
	return 0;
}

static void __exit end_module(void)
{
	for_each_possible_cpu(cpu) {
		printk(KERN_INFO
		       "percpu_test: cpu[%d] => %d(macro), %d(dynamic)",
		       cpu, per_cpu(my_percpu_macro, cpu),
		       *per_cpu_ptr(my_percpu_dynamic, cpu));
	}
	free_percpu(my_percpu_dynamic);
	printk(KERN_INFO "percpu_test: module unloaded\n");
}

MODULE_AUTHOR("Kunwook Park");

MODULE_LICENSE("GPL");

module_init(start_module);
module_exit(end_module);
