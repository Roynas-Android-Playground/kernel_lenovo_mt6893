/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>

#define TAG "[LT]"

struct LT_USER_DATA {
	void (*fn)(int mask_loading, int loading);
	unsigned long polling_ms;
	const struct cpumask *cpu_mask;
	u64 *prev_idle_time;
	u64 *prev_wall_time;
	cpumask_t prev_online_cpus;
	struct hlist_node user_list_node;
	struct LT_WORK_DATA *link2work;
};

struct LT_WORK_DATA {
	struct LT_USER_DATA *link2user;
	struct delayed_work s_work;
	ktime_t target_expire;
};

static int nr_cpus;

static struct workqueue_struct *ps_lk_wq;
static HLIST_HEAD(lt_user_list);
DEFINE_MUTEX(lt_mlock);
static void lt_work_fn(struct work_struct *ps_work);

static inline void lt_lock(const char *tag)
{
	mutex_lock(&lt_mlock);
}

static inline void lt_unlock(const char *tag)
{
	mutex_unlock(&lt_mlock);
}

static inline void lt_lockprove(const char *tag)
{
	WARN_ON(!mutex_is_locked(&lt_mlock));
}

static void *new_lt_work(struct LT_USER_DATA *lt_user)
{
	struct LT_WORK_DATA *new_work;

	lt_lockprove(__func__);
	new_work = kzalloc(sizeof(*new_work), GFP_KERNEL);
	if (!new_work)
		goto new_lt_work_out;

	new_work->link2user = lt_user;
	INIT_DELAYED_WORK(&new_work->s_work, lt_work_fn);
	new_work->target_expire =
		ktime_add_ms(ktime_get(), lt_user->polling_ms);

new_lt_work_out:
	return new_work;
}

static inline void free_lt_work(struct LT_WORK_DATA *lt_work)
{
	lt_lockprove(__func__);
	kfree(lt_work);
}

static int lt_calculate_loading(u64 idle_time, u64 wall_time)
{
	int ret = -EOVERFLOW;

	if (wall_time > 0 && wall_time >= idle_time)
		ret = div_u64((wall_time - idle_time) * 100, wall_time);

	return ret;
}

static void lt_update_loading(struct LT_USER_DATA *lt_data,
			      int *mask_loading, int *loading)
{
	int cpu;
	bool mask_is_all;
	u64 cur_idle_time_i, cur_wall_time_i;
	u64 delta_idle_time, delta_wall_time;
	u64 cpu_idle_time = 0, cpu_wall_time = 0;
	u64 mask_idle_time = 0, mask_wall_time = 0;

	lt_lockprove(__func__);
	mask_is_all = cpumask_equal(cpu_possible_mask, lt_data->cpu_mask);
	*mask_loading = mask_is_all ? -ENODATA : -EOVERFLOW;
	*loading = -EOVERFLOW;

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		if (!cpu_online(cpu)) {
			cpumask_clear_cpu(cpu, &lt_data->prev_online_cpus);
			continue;
		}

		cur_idle_time_i = get_cpu_idle_time(cpu, &cur_wall_time_i, 1);

		if (!cpumask_test_cpu(cpu, &lt_data->prev_online_cpus)) {
			lt_data->prev_idle_time[cpu] = cur_idle_time_i;
			lt_data->prev_wall_time[cpu] = cur_wall_time_i;
			cpumask_set_cpu(cpu, &lt_data->prev_online_cpus);
			continue;
		}

		if (cur_idle_time_i < lt_data->prev_idle_time[cpu] ||
		    cur_wall_time_i < lt_data->prev_wall_time[cpu])
			goto update_baseline;

		delta_idle_time = cur_idle_time_i - lt_data->prev_idle_time[cpu];
		delta_wall_time = cur_wall_time_i - lt_data->prev_wall_time[cpu];
		if (!delta_wall_time || delta_idle_time > delta_wall_time)
			goto update_baseline;

		if (!cpu_isolated(cpu)) {
			cpu_idle_time += delta_idle_time;
			cpu_wall_time += delta_wall_time;

			if (!mask_is_all &&
			    cpumask_test_cpu(cpu, lt_data->cpu_mask)) {
				mask_idle_time += delta_idle_time;
				mask_wall_time += delta_wall_time;
			}
		}

update_baseline:
		lt_data->prev_idle_time[cpu] = cur_idle_time_i;
		lt_data->prev_wall_time[cpu] = cur_wall_time_i;
	}
	put_online_cpus();

	*loading = lt_calculate_loading(cpu_idle_time, cpu_wall_time);
	if (!mask_is_all)
		*mask_loading = lt_calculate_loading(mask_idle_time,
						     mask_wall_time);
}

static void lt_work_fn(struct work_struct *ps_work)
{
	struct delayed_work *dwork;
	struct LT_WORK_DATA *lt_work;
	struct LT_USER_DATA *lt_user;
	ktime_t ktime_now;
	int mask_loading, loading;

	lt_lock(__func__);
	dwork = container_of(ps_work, struct delayed_work, work);
	lt_work = container_of(dwork, struct LT_WORK_DATA, s_work);
	lt_user = lt_work->link2user;
	if (lt_user) {
		lt_update_loading(lt_user, &mask_loading, &loading);
		lt_user->fn(mask_loading, loading);

		ktime_now = ktime_get();
		do {
			lt_work->target_expire =
				ktime_add_ms(lt_work->target_expire,
					lt_user->polling_ms);
		} while (!ktime_after(lt_work->target_expire, ktime_now));

		queue_delayed_work(ps_lk_wq, &lt_work->s_work,
			msecs_to_jiffies(ktime_ms_delta(
				lt_work->target_expire, ktime_now)));
	} else
		free_lt_work(lt_work);

	lt_unlock(__func__);
}

static void *new_lt_user(void (*fn)(int mask_loading, int loading),
			 unsigned long polling_ms, const struct cpumask *cpu_mask)
{
	struct LT_USER_DATA *new_lt;
	int cpu;

	lt_lockprove(__func__);
	new_lt                 = kzalloc(sizeof(*new_lt), GFP_KERNEL);
	if (!new_lt)
		goto new_lt_alloc_err;

	new_lt->fn             = fn;
	new_lt->polling_ms     = polling_ms;
	new_lt->cpu_mask       = cpu_mask;
	new_lt->prev_idle_time =
		kcalloc(nr_cpus, sizeof(u64), GFP_KERNEL);
	if (!new_lt->prev_idle_time)
		goto new_lt_idle_alloc_err;

	new_lt->prev_wall_time =
		kcalloc(nr_cpus, sizeof(u64), GFP_KERNEL);
	if (!new_lt->prev_wall_time)
		goto new_lt_wall_alloc_err;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		new_lt->prev_idle_time[cpu] =
			get_cpu_idle_time(cpu,
					  &new_lt->prev_wall_time[cpu], 1);
		cpumask_set_cpu(cpu, &new_lt->prev_online_cpus);
	}
	put_online_cpus();

	hlist_add_head(&new_lt->user_list_node, &lt_user_list);

	return new_lt;

new_lt_wall_alloc_err:
	kfree(new_lt->prev_idle_time);
new_lt_idle_alloc_err:
	kfree(new_lt);
new_lt_alloc_err:
	return NULL;
}

static void free_lt_user(struct LT_USER_DATA *node)
{
	lt_lockprove(__func__);
	if (node->link2work)
		node->link2work->link2user = NULL;

	hlist_del(&node->user_list_node);
	kfree(node->prev_idle_time);
	kfree(node->prev_wall_time);
	kfree(node);
}

static void lt_cleanup(void)
{
	struct LT_USER_DATA *ltiter = NULL;
	struct hlist_node *t;

	lt_lock(__func__);
	hlist_for_each_entry_safe(ltiter, t, &lt_user_list, user_list_node)
		free_lt_user(ltiter);

	lt_unlock(__func__);
}

int reg_loading_tracking_sp(void (*fn)(int mask_loading, int loading),
			    unsigned long polling_ms, const struct cpumask *cpu_mask,
			    const char *caller)
{
	struct LT_USER_DATA *ltiter = NULL, *new_user;
	struct LT_WORK_DATA *new_work;
	int ret = 0;

	might_sleep();

	lt_lock(__func__);
	if (!fn || polling_ms == 0 || !ps_lk_wq) {
		ret = -EINVAL;
		goto reg_loading_tracking_out;
	}

	hlist_for_each_entry(ltiter, &lt_user_list, user_list_node)
		if (ltiter->fn == fn)
			break;

	if (ltiter) {
		ret = -EINVAL;
		goto reg_loading_tracking_out;
	}

	new_user = new_lt_user(fn, polling_ms, cpu_mask);
	if (!new_user) {
		ret = -ENOMEM;
		goto reg_loading_tracking_out;
	}

	new_work = new_lt_work(new_user);
	if (!new_work)
		goto reg_loading_tracking_new_work_err;

	new_user->link2work = new_work;

	queue_delayed_work(ps_lk_wq, &new_work->s_work,
		msecs_to_jiffies(new_user->polling_ms));

	pr_debug(TAG"%s %s success\n", __func__, caller);

	goto reg_loading_tracking_out;

reg_loading_tracking_new_work_err:
	free_lt_user(new_user);
reg_loading_tracking_out:
	lt_unlock(__func__);

	return ret;
}


int unreg_loading_tracking_sp(void (*fn)(int mask_loading, int loading),
			      const char *caller)
{
	struct LT_USER_DATA *ltiter = NULL;
	int ret = 0;

	might_sleep();

	lt_lock(__func__);
	if (!fn || !ps_lk_wq) {
		ret = -EINVAL;
		goto unreg_loading_tracking_out;
	}

	hlist_for_each_entry(ltiter, &lt_user_list, user_list_node)
		if (ltiter->fn == fn)
			break;

	if (!ltiter) {
		ret = -EINVAL;
		goto unreg_loading_tracking_out;
	}

	free_lt_user(ltiter);
	pr_debug(TAG"%s %s success\n", __func__, caller);

unreg_loading_tracking_out:
	lt_unlock(__func__);

	return ret;
}

static int __init load_track_init(void)
{
	/* CPU IDs need not be densely packed in cpu_possible_mask. */
	nr_cpus = nr_cpu_ids;
	ps_lk_wq = create_workqueue("lt_wq");
	if (!ps_lk_wq) {
		return -EFAULT;
		pr_debug(TAG"%s OOM\n", __func__);
	}

	return 0;
}

static void __exit load_track_exit(void)
{
	lt_cleanup();
	flush_workqueue(ps_lk_wq);
	destroy_workqueue(ps_lk_wq);
}

module_init(load_track_init);
module_exit(load_track_exit);
