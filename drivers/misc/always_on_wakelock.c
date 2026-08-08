// SPDX-License-Identifier: GPL-2.0
/*
 * Always-on debug wakelock: holds a permanent wakeup_source so the
 * system never enters suspend/s2idle, for isolating issues from
 * PM-suspend-triggered instability during debugging.
 *
 * Runtime knobs under /sys/module/always_on_wakelock/parameters/:
 *   disabled - rw bool, 1 releases the lock and keeps it released,
 *              0 (re)acquires it. Also settable on the kernel
 *              cmdline (always_on_wakelock.disabled=1) to skip the
 *              initial acquire at boot.
 *   release  - write-only trigger, releases the lock once.
 *   acquire  - write-only trigger, (re)acquires the lock.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pm_wakeup.h>

static struct wakeup_source *always_on_ws;
static bool wl_disabled;

static void wl_do_release(void)
{
	__pm_relax(always_on_ws);
	wl_disabled = true;
}

static void wl_do_acquire(void)
{
	__pm_stay_awake(always_on_ws);
	wl_disabled = false;
}

static int wl_disabled_set(const char *val, const struct kernel_param *kp)
{
	bool new_val;
	int ret = kstrtobool(val, &new_val);

	if (ret)
		return ret;

	if (new_val)
		wl_do_release();
	else
		wl_do_acquire();
	return 0;
}

static const struct kernel_param_ops wl_disabled_ops = {
	.set = wl_disabled_set,
	.get = param_get_bool,
};
module_param_cb(disabled, &wl_disabled_ops, &wl_disabled, 0644);
MODULE_PARM_DESC(disabled, "Release (1) or hold (0) the always-on debug wakelock");

static int wl_release_set(const char *val, const struct kernel_param *kp)
{
	wl_do_release();
	return 0;
}

static const struct kernel_param_ops wl_release_ops = {
	.set = wl_release_set,
};
module_param_cb(release, &wl_release_ops, NULL, 0200);
MODULE_PARM_DESC(release, "Write any value to release the always-on debug wakelock once");

static int wl_acquire_set(const char *val, const struct kernel_param *kp)
{
	wl_do_acquire();
	return 0;
}

static const struct kernel_param_ops wl_acquire_ops = {
	.set = wl_acquire_set,
};
module_param_cb(acquire, &wl_acquire_ops, NULL, 0200);
MODULE_PARM_DESC(acquire, "Write any value to (re)acquire the always-on debug wakelock");

static int __init always_on_wakelock_init(void)
{
	always_on_ws = wakeup_source_register(NULL, "always-on-debug");
	if (!always_on_ws)
		return -ENOMEM;

	if (!wl_disabled)
		wl_do_acquire();

	pr_info("always_on_wakelock: %s\n",
		wl_disabled ? "disabled at boot" : "held, system will not suspend");
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
