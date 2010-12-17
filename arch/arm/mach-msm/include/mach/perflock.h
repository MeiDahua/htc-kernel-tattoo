/* arch/arm/mach-msm/perflock.h
 *
 * MSM performance lock driver header
 *
 * Copyright (C) 2008 HTC Corporation
 * Author: Eiven Peng <eiven_peng@htc.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_PERF_LOCK_H
#define __ARCH_ARM_MACH_PERF_LOCK_H

#include <linux/list.h>

/*
 * Performance level determine differnt EBI1 rate
 */

enum {
	PERF_LOCK_MEDIUM,	/* Medium performance */
	PERF_LOCK_HIGH,	/* High performance */
	PERF_LOCK_HIGHEST,	/* Highest performance */
	PERF_LOCK_INVALID,
};

struct perf_lock {
	struct list_head link;
	unsigned int flags;
	unsigned int level;
	const char *name;
};

struct perflock_platform_data {
	unsigned int *perf_acpu_table;
	unsigned int table_size;
};

#ifndef CONFIG_PERFLOCK
static inline void __init perflock_init(struct perflock_platform_data *pdata) { return; }
static inline void perf_lock_init(struct perf_lock *lock,
	unsigned int level, const char *name) { return; }
static inline void perf_lock(struct perf_lock *lock) { return; }
static inline void perf_unlock(struct perf_lock *lock) { return; }
static inline int is_perf_lock_active(struct perf_lock *lock) { return 0; }
static inline int is_perf_locked(void) { return 0; }
#else
extern void __init perflock_init(struct perflock_platform_data *pdata);
extern void perf_lock_init(struct perf_lock *lock,
	unsigned int level, const char *name);
extern void perf_lock(struct perf_lock *lock);
extern void perf_unlock(struct perf_lock *lock);
extern int is_perf_lock_active(struct perf_lock *lock);
extern int is_perf_locked(void);
#endif


#endif

