/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#define pr_fmt(fmt) "cpuhp: " fmt

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <mtk_ppm_api.h>
#include <linux/nmi.h>

#include "mtk_cpuhp_private.h"

static struct cpumask ppm_online_cpus;
static struct cpumask ppm_allowed_cpus;
static struct task_struct *ppm_kthread;
static DEFINE_MUTEX(ppm_mutex);
static DECLARE_WAIT_QUEUE_HEAD(ppm_wait_queue);

#ifdef CONFIG_PM_SLEEP
static struct wakeup_source *hps_ws;
#endif

#define HPS_RETRY	10

static bool ppm_request_pending(void)
{
	struct cpumask requested;

	mutex_lock(&ppm_mutex);
	cpumask_and(&requested, &ppm_online_cpus, &ppm_allowed_cpus);
	mutex_unlock(&ppm_mutex);

	return !cpumask_equal(&requested, cpu_online_mask);
}

static int ppm_thread_fn(void *data)
{
	int request_cpu_up;
	int i;
	int rc;

	struct cpumask ppm_cpus_req;

	while (!kthread_should_stop()) {
		rc = wait_event_interruptible(ppm_wait_queue,
			kthread_should_stop() || ppm_request_pending());
		if (kthread_should_stop())
			break;
		if (rc)
			continue;

		mutex_lock(&ppm_mutex);
		cpumask_and(&ppm_cpus_req, &ppm_online_cpus,
			    &ppm_allowed_cpus);
		mutex_unlock(&ppm_mutex);

#ifdef CONFIG_PM_SLEEP
		if (hps_ws)
			__pm_stay_awake(hps_ws);
#endif

		pr_debug_ratelimited("%s: ppm_cpus_req: %*pbl cpu_online_mask: %*pbl\n"
			, __func__
			, cpumask_pr_args(&ppm_cpus_req)
			, cpumask_pr_args(cpu_online_mask));


		/* process the request of up each CPUs from PPM */
		for_each_possible_cpu(i) {
			struct device *cpu_dev;

			request_cpu_up = cpumask_test_cpu(i, &ppm_cpus_req);

			if (request_cpu_up && cpu_is_offline(i)) {
				int retry = 0;

				pr_debug_ratelimited("CPU%d: ppm-request=%d, offline->powerup\n",
					 i, request_cpu_up);
Retry_ON:
				cpu_dev = get_cpu_device(i);
				if (!cpu_dev) {
					pr_info("get cpu%d fail!\n", i);
					continue;
				}

				rc = device_online(cpu_dev);
				if (rc)	{
					if (retry > HPS_RETRY) {
						pr_debug_ratelimited(
							"fail to bringup cpu(%d) rc: %d\n"
							, i, rc);
						trigger_all_cpu_backtrace();
						continue;
					}
					retry++;
					goto Retry_ON;
				}
				continue;
			}
		}

		/* process the request of down each CPUs from PPM */
		for_each_possible_cpu(i) {
			struct device *cpu_dev;

			request_cpu_up = cpumask_test_cpu(i, &ppm_cpus_req);

			if (!request_cpu_up && cpu_online(i)) {
				int retry = 0;

				pr_debug_ratelimited("CPU%d: ppm-request=%d, online->powerdown\n",
					 i, request_cpu_up);
Retry_OFF:
				cpu_dev = get_cpu_device(i);
				if (!cpu_dev) {
					pr_info("get cpu%d fail!\n", i);
					continue;
				}

				rc = device_offline(cpu_dev);
				if (rc) {
					if (retry > HPS_RETRY) {
						pr_debug_ratelimited(
							"fail to shutdown cpu(%d) rc: %d\n"
							, i, rc);
						trigger_all_cpu_backtrace();
						continue;
					}
					retry++;
					goto Retry_OFF;
				}
				continue;
			}
		}

#ifdef CONFIG_PM_SLEEP
		if (hps_ws)
			__pm_relax(hps_ws);
#endif

	}

	return 0;
}

static void ppm_init_allowed_cpus(void)
{
	unsigned int cpu;

	if (setup_max_cpus < num_present_cpus())
		cpumask_copy(&ppm_allowed_cpus, cpu_online_mask);
	else
		cpumask_copy(&ppm_allowed_cpus, cpu_present_mask);

	for_each_possible_cpu(cpu) {
		if (cpu_is_quarantined(cpu))
			cpumask_clear_cpu(cpu, &ppm_allowed_cpus);
	}

	cpumask_set_cpu(get_boot_cpu_id(), &ppm_allowed_cpus);

	pr_info("maxcpus=%u and quarantine limit PPM CPUs to %*pbl\n",
		setup_max_cpus, cpumask_pr_args(&ppm_allowed_cpus));
}

static void ppm_apply_cpu_limit(cpumask_t *cpus)
{
	cpumask_and(cpus, cpus, &ppm_allowed_cpus);
	cpumask_set_cpu(get_boot_cpu_id(), cpus);
}

static void ppm_limit_callback(struct ppm_client_req req)
{
	mutex_lock(&ppm_mutex);
	cpumask_copy(&ppm_online_cpus, &req.online_core[0]);
	ppm_apply_cpu_limit(&ppm_online_cpus);
	mutex_unlock(&ppm_mutex);

	wake_up_interruptible(&ppm_wait_queue);
}


void ppm_notifier(void)
{
	unsigned int cpu;
	struct device_node *dn = 0;
	const char *smp_method = 0;
	ppm_init_allowed_cpus();
	cpumask_copy(&ppm_online_cpus, cpu_online_mask);
	ppm_apply_cpu_limit(&ppm_online_cpus);

	for_each_present_cpu(cpu) {
		dn = of_get_cpu_node(cpu, NULL);
		smp_method = of_get_property(dn, "smp-method", NULL);
		if (smp_method != NULL) {
			if (!strcmp("disabled", smp_method)) {
				pr_info("[ENTER Hotplug DEBUG MODE!!!]\n");
				return;
			}
		}
	}

	/* create a kthread to serve the requests from PPM */
	ppm_kthread = kthread_create(ppm_thread_fn, NULL, "cpuhp-ppm");
	if (IS_ERR(ppm_kthread)) {
		pr_notice("error creating ppm kthread (%ld)\n",
		       PTR_ERR(ppm_kthread));
		return;
	}
	wake_up_process(ppm_kthread);

	/* register PPM callback */
	mt_ppm_register_client(PPM_CLIENT_HOTPLUG, &ppm_limit_callback);

	hps_ws = wakeup_source_register(NULL, "hps");
	if (!hps_ws)
		pr_debug("hps wakelock register fail!\n");
}
