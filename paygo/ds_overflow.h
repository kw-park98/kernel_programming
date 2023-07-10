#ifndef DS_OVERFLOW_H__
#define DS_OVERFLOW_H__

#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/spinlock.h>

struct paygo_overflow_list {
	spinlock_t lock;
	struct list_head head;
};


struct perobj_overflow_list {
	void *obj;
	struct paygo_overflow_list overflow;
	struct hlist_node hnode;
};

#endif // DS_OVERFLOW_H__
