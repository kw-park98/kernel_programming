/* Linux Kernel v6.2 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/unistd.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>

#define __NR_MYOPEN 548
#define __NR_MYPREAD 549

static unsigned long **sys_call_table;

static uid_t uid = -1;
module_param(uid, int, 0644);

static asmlinkage long (*original_openat)(const struct pt_regs *);
static asmlinkage long (*original_pread64)(const struct pt_regs *);
static asmlinkage long (*original_close)(const struct pt_regs *);

/*
#define LEN_BUF (128)
static asmlinkage long our_sys_openat(const struct pt_regs *regs)
{
	int i = 0;
	int flags, mode;
	char ch;
	char filename[LEN_BUF] = {
		0,
	};

	if (__kuid_val(current_uid()) != uid)
		goto orig_call;

	do {
		get_user(ch, (char __user *)regs->si + i);
		filename[i] = ch;
		i++;
	} while (ch != 0 && i < (LEN_BUF - 1));
	filename[i] = '\0';
	flags = (int)regs->dx;
	mode = (int)regs->cx;
	pr_info("[%d] open(\"%s\", %d, %d)", current->pid, filename, flags,
		mode);

orig_call:
	return original_openat(regs);
}

static asmlinkage long our_sys_pread64(const struct pt_regs *regs)
{
	int fd;
	char *buf;
	size_t count;
	loff_t pos;

	if (__kuid_val(current_uid()) != uid)
		goto orig_call;
	fd = (int)regs->di;
	buf = (char *)regs->si;
	count = (size_t)regs->dx;
	pos = (loff_t)regs->cx;
	pr_info("[%d] sys_pread64(%d, %p, %lu, %lld)", current->pid, fd, buf,
		count, pos);
orig_call:
	return original_pread64(regs);
}
*/

static asmlinkage long our_sys_close(const struct pt_regs *regs)
{
	int fd;

	if (__kuid_val(current_uid()) != uid)
		goto orig_call;
	fd = (int)regs->di;
	pr_info("[%d] sys_close(%d)", current->pid, fd);
orig_call:
	return original_close(regs);
}

static unsigned long **acquire_sys_call_table(void)
{
	unsigned long (*kallsyms_lookup_name)(const char *name);
	struct kprobe kp = {
		.symbol_name = "kallsyms_lookup_name",
	};

	if (register_kprobe(&kp) < 0)
		return NULL;
	kallsyms_lookup_name = (unsigned long (*)(const char *name))kp.addr;
	unregister_kprobe(&kp);

	return (unsigned long **)kallsyms_lookup_name("sys_call_table");
}

static inline void __write_cr0(unsigned long cr0)
{
	asm volatile("mov %0,%%cr0" : "+r"(cr0) : : "memory");
}

static void enable_write_protection(void)
{
	unsigned long cr0 = read_cr0();
	set_bit(16, &cr0);
	__write_cr0(cr0);
}

static void disable_write_protection(void)
{
	unsigned long cr0 = read_cr0();
	clear_bit(16, &cr0);
	__write_cr0(cr0);
}

static int __init syscall_start(void)
{
	if (!(sys_call_table = acquire_sys_call_table()))
		return -1;

	disable_write_protection();

	original_openat = (void *)sys_call_table[__NR_openat];
	original_pread64 = (void *)sys_call_table[__NR_pread64];
	original_close = (void *)sys_call_table[__NR_close];

	sys_call_table[__NR_openat] = sys_call_table[__NR_MYOPEN];
	sys_call_table[__NR_pread64] = sys_call_table[__NR_MYPREAD];
	sys_call_table[__NR_close] = (unsigned long *)our_sys_close;

	enable_write_protection();

	pr_info("Spying on UID: %d\n", uid);

	return 0;
}

static void __exit syscall_end(void)
{
	if (!sys_call_table)
		return;

	disable_write_protection();
	sys_call_table[__NR_openat] = (unsigned long *)original_openat;
	sys_call_table[__NR_pread64] = (unsigned long *)original_pread64;
	sys_call_table[__NR_close] = (unsigned long *)original_close;
	enable_write_protection();

	msleep(1000);
}

module_init(syscall_start);
module_exit(syscall_end);

MODULE_AUTHOR("Hyeonmin Lee");
MODULE_LICENSE("GPL");
