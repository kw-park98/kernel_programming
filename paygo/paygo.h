#ifndef __PAYGO_H__
#define __PAYGO_H__

/* Functions

-------------------------------------------------------
These functions are APIs for reference counting.

0. init_paygo

1. paygo_ref
2. paygo_unref 

3. paygo_read (not yet)
-------------------------------------------------------
*/

/**
 * init_paygo_table
 *
 * allocate memory for hashtable and initialize the table
 */
void init_paygo_table(void);

/**
 * paygo_ref
 *
 * @obj: pointer of the object
 *
 * @thread_id: this is thread id used only in tests instead of task struct
 *
 * Return: 0 for success
 *
 * increment reference count of the object.
 *
 */
int paygo_inc(void *obj);

/**
 * paygo_unref
 *
 * @obj: pointer of the object
 *
 * @thread_id: this is thread id used only in tests instead of task struct
 *
 * Return: 0 for success
 *
 * decrement reference count of the object.
 *
 */
int paygo_dec(void *obj);

/**
 * paygo_read
 *
 * @obj: pointer of the object
 *
 * Return: True when the total count is zero, False when else
 *
 */
bool paygo_read(void *obj);

/**
 * traverse_paygo
 *
 * Context: This function must run alone. 
 *
 * Traverse hashtables and print about all of the per-cpu hashtable's entries.
 *
 */
void traverse_paygo(void);

#endif // __PAYGO_H__
