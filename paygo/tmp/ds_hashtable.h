#ifndef DS_HASHTABLE_H__
#define DS_HASHTABLE_H__

#include "ds_overflow.h"

#define TSIZE 64

extern struct paygo_overflow_list overflow;
extern void put_entry(struct paygo_overflow_list *over_flowlist, struct paygo_entry *entry);
extern struct paygo_entry *get_entry(struct paygo_overflow_list *overflow_list, void *obj);

struct paygo_hashtable {
	struct paygo_entry entries[TSIZE];	
}; 


#endif // DS_HASHTABLE_H__ 
