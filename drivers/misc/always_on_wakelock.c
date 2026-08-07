// SPDX-License-Identifier: GPL-2.0
/*
 * Always-on debug wakelock: holds a permanent wakeup_source so the
 * system never enters suspend/s2idle, for isolating issues from
 * PM-suspend-triggered instability during debugging.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pm_wakeup.h>

static struct wakeup_source *always_on_ws;

static int __init always_on_wakelock_init(void)
{
	always_on_ws = wakeup_source_register(NULL, "always-on-debug");
	if (!always_on_ws)
		return -ENOMEM;

	__pm_stay_awake(always_on_ws);
	pr_info("always_on_wakelock: held, system will not suspend\n");
	return 0;
}

static void __exit always_on_wakelock_exit(void)
{
	__pm_relax(always_on_ws);
	wakeup_source_unregister(always_on_ws);
}

module_init(always_on_wakelock_init);
module_exit(always_on_wakelock_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Always-on debug wakelock");
