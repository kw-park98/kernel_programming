#ifndef DS_PAYGO_ENTRY_H__
#define DS_PAYGO_ENTRY_H__

#include <linux/list.h>
#include <linux/spinlock.h>

struct __attribute__((aligned(64))) paygo_entry {
	void *obj;
	int local_count;
	int anchor_count;
	struct list_head list;
	int cpu;
};

#endif // DS_PAYGO_ENTRY_H__
