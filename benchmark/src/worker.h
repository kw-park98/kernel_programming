/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKM_WORK_H
#define _LKM_WORK_H

struct worker {
	int id;
	struct task_struct *task;
	volatile long retval;
} ____cacheline_aligned_in_smp;

extern void worker_setup(void);
extern void worker_routine(void);
extern long worker_teardown(void);

#endif /* _LKM_WORK_H */
