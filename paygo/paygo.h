#ifndef __PAYGO_H__
#define __PAYGO_H__

/* Functions

-------------------------------------------------------
These functions are APIs for reference counting.

1. paygo_ref
2. paygo_unref 

3. paygo_read (not yet)
-------------------------------------------------------
*/

/**
 * paygo_ref
 *
 * @obj: pointer of the object
 *
 * Return: 0 for success
 *
 * increment reference count of the object.
 *
 */
int paygo_ref(void *obj, int thread_id);

/**
 * paygo_unref
 *
 * @obj: pointer of the object
 *
 * Return: 0 for success
 *
 * decrement reference count of the object.
 *
 */
int paygo_unref(void *obj, int thread_id);


#endif // __PAYGO_H__
