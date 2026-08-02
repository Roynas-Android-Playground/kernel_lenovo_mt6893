/*
 * AArch64 KGDB support
 *
 * Based on arch/arm/kernel/kgdb.c
 *
 * Copyright (C) 2013 Cavium Inc.
 * Author: Vijaya Kumar K <vijaya.kumar@caviumnetworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/hw_breakpoint.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kdebug.h>
#include <linux/kgdb.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/sched/task_stack.h>
#include <linux/smp.h>
#include <linux/suspend.h>

#include <asm/debug-monitors.h>
#include <asm/hw_breakpoint.h>
#include <asm/insn.h>
#include <asm/traps.h>

struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] = {
	{ "x0", 8, offsetof(struct pt_regs, regs[0])},
	{ "x1", 8, offsetof(struct pt_regs, regs[1])},
	{ "x2", 8, offsetof(struct pt_regs, regs[2])},
	{ "x3", 8, offsetof(struct pt_regs, regs[3])},
	{ "x4", 8, offsetof(struct pt_regs, regs[4])},
	{ "x5", 8, offsetof(struct pt_regs, regs[5])},
	{ "x6", 8, offsetof(struct pt_regs, regs[6])},
	{ "x7", 8, offsetof(struct pt_regs, regs[7])},
	{ "x8", 8, offsetof(struct pt_regs, regs[8])},
	{ "x9", 8, offsetof(struct pt_regs, regs[9])},
	{ "x10", 8, offsetof(struct pt_regs, regs[10])},
	{ "x11", 8, offsetof(struct pt_regs, regs[11])},
	{ "x12", 8, offsetof(struct pt_regs, regs[12])},
	{ "x13", 8, offsetof(struct pt_regs, regs[13])},
	{ "x14", 8, offsetof(struct pt_regs, regs[14])},
	{ "x15", 8, offsetof(struct pt_regs, regs[15])},
	{ "x16", 8, offsetof(struct pt_regs, regs[16])},
	{ "x17", 8, offsetof(struct pt_regs, regs[17])},
	{ "x18", 8, offsetof(struct pt_regs, regs[18])},
	{ "x19", 8, offsetof(struct pt_regs, regs[19])},
	{ "x20", 8, offsetof(struct pt_regs, regs[20])},
	{ "x21", 8, offsetof(struct pt_regs, regs[21])},
	{ "x22", 8, offsetof(struct pt_regs, regs[22])},
	{ "x23", 8, offsetof(struct pt_regs, regs[23])},
	{ "x24", 8, offsetof(struct pt_regs, regs[24])},
	{ "x25", 8, offsetof(struct pt_regs, regs[25])},
	{ "x26", 8, offsetof(struct pt_regs, regs[26])},
	{ "x27", 8, offsetof(struct pt_regs, regs[27])},
	{ "x28", 8, offsetof(struct pt_regs, regs[28])},
	{ "x29", 8, offsetof(struct pt_regs, regs[29])},
	{ "x30", 8, offsetof(struct pt_regs, regs[30])},
	{ "sp", 8, offsetof(struct pt_regs, sp)},
	{ "pc", 8, offsetof(struct pt_regs, pc)},
	/*
	 * struct pt_regs thinks PSTATE is 64-bits wide but gdb remote
	 * protocol disagrees. Therefore we must extract only the lower
	 * 32-bits. Look for the big comment in asm/kgdb.h for more
	 * detail.
	 */
	{ "pstate", 4, offsetof(struct pt_regs, pstate)
#ifdef CONFIG_CPU_BIG_ENDIAN
							+ 4
#endif
	},
	{ "v0", 16, -1 },
	{ "v1", 16, -1 },
	{ "v2", 16, -1 },
	{ "v3", 16, -1 },
	{ "v4", 16, -1 },
	{ "v5", 16, -1 },
	{ "v6", 16, -1 },
	{ "v7", 16, -1 },
	{ "v8", 16, -1 },
	{ "v9", 16, -1 },
	{ "v10", 16, -1 },
	{ "v11", 16, -1 },
	{ "v12", 16, -1 },
	{ "v13", 16, -1 },
	{ "v14", 16, -1 },
	{ "v15", 16, -1 },
	{ "v16", 16, -1 },
	{ "v17", 16, -1 },
	{ "v18", 16, -1 },
	{ "v19", 16, -1 },
	{ "v20", 16, -1 },
	{ "v21", 16, -1 },
	{ "v22", 16, -1 },
	{ "v23", 16, -1 },
	{ "v24", 16, -1 },
	{ "v25", 16, -1 },
	{ "v26", 16, -1 },
	{ "v27", 16, -1 },
	{ "v28", 16, -1 },
	{ "v29", 16, -1 },
	{ "v30", 16, -1 },
	{ "v31", 16, -1 },
	{ "fpsr", 4, -1 },
	{ "fpcr", 4, -1 },
};

char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return NULL;

	if (dbg_reg_def[regno].offset != -1)
		memcpy(mem, (void *)regs + dbg_reg_def[regno].offset,
		       dbg_reg_def[regno].size);
	else
		memset(mem, 0, dbg_reg_def[regno].size);
	return dbg_reg_def[regno].name;
}

int dbg_set_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return -EINVAL;

	if (dbg_reg_def[regno].offset != -1)
		memcpy((void *)regs + dbg_reg_def[regno].offset, mem,
		       dbg_reg_def[regno].size);
	return 0;
}

void
sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *task)
{
	struct pt_regs *thread_regs;

	/* Initialize to zero */
	memset((char *)gdb_regs, 0, NUMREGBYTES);
	thread_regs = task_pt_regs(task);
	memcpy((void *)gdb_regs, (void *)thread_regs->regs, GP_REG_BYTES);
	/* Special case for PSTATE (check comments in asm/kgdb.h for details) */
	dbg_get_reg(33, gdb_regs + GP_REG_BYTES, thread_regs);
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	regs->pc = pc;
}

static int compiled_break;
static DEFINE_PER_CPU(bool, kgdb_step_ref_owned);
static DEFINE_MUTEX(kgdb_hw_init_lock);
static bool kgdb_hw_init_requested;
static bool kgdb_hw_smp_ready;

static bool kgdb_step_ref_is_owned(void)
{
	return this_cpu_read(kgdb_step_ref_owned);
}
NOKPROBE_SYMBOL(kgdb_step_ref_is_owned);

static void kgdb_step_ref_get(struct pt_regs *regs)
{
	if (WARN_ON_ONCE(kgdb_step_ref_is_owned()))
		return;

	kernel_enable_single_step(regs);
	this_cpu_write(kgdb_step_ref_owned, true);
}
NOKPROBE_SYMBOL(kgdb_step_ref_get);

static bool kgdb_step_ref_take(void)
{
	if (!kgdb_step_ref_is_owned())
		return false;

	this_cpu_write(kgdb_step_ref_owned, false);
	return true;
}
NOKPROBE_SYMBOL(kgdb_step_ref_take);

static bool kgdb_step_ref_put(void)
{
	if (!kgdb_step_ref_take())
		return false;

	/* Balance our debug-monitor ref even if another owner cleared SS. */
	kernel_disable_single_step();
	return true;
}
NOKPROBE_SYMBOL(kgdb_step_ref_put);

enum kgdb_hw_resume_mode {
	KGDB_HW_RESUME_NONE,
	KGDB_HW_RESUME_CONTINUE,
	KGDB_HW_RESUME_STEP,
};

enum kgdb_hw_step_action {
	KGDB_HW_STEP_NONE,
	KGDB_HW_STEP_PASS,
	KGDB_HW_STEP_CONSUME,
	KGDB_HW_STEP_REPORT,
};

#ifdef CONFIG_HAVE_HW_BREAKPOINT
#define KGDB_HW_MAX_SLOTS	(ARM_MAX_BRP + ARM_MAX_WRP)

struct kgdb_hw_breakpoint {
	bool enabled;
	bool retiring;
	u64 generation;
	u64 retired_generation;
	unsigned long addr;
	int len;
	enum kgdb_bptype bptype;
	int perf_type;
	struct perf_event * __percpu *events;
	u64 __percpu *armed_generation;
	cpumask_t reserved_cpus;
	cpumask_t installed_cpus;
	cpumask_t releasing_cpus;
};

enum kgdb_hw_cpu_phase {
	KGDB_HW_CPU_IDLE,
	KGDB_HW_CPU_HIT,
	KGDB_HW_CPU_STEP,
};

struct kgdb_hw_cpu_state {
	enum kgdb_hw_cpu_phase phase;
	enum kgdb_hw_resume_mode resume_mode;
	unsigned long hit_pc;
	u64 hit_generation;
	bool hit_from_kgdb_step;
	bool step_owned;
};

static struct kgdb_hw_breakpoint kgdb_hw_breakpoints[KGDB_HW_MAX_SLOTS];
static unsigned int kgdb_hw_slot_count;
static atomic64_t kgdb_hw_generation = ATOMIC64_INIT(0);
static DEFINE_PER_CPU(struct kgdb_hw_cpu_state, kgdb_hw_cpu_state);
static cpumask_t kgdb_hw_cpu_mask;
static cpumask_t kgdb_hw_quiesced_cpus;
static bool kgdb_hw_hotplug_disabled;
static bool kgdb_hw_pm_registered;

static int kgdb_hw_pm_notify(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		pr_warn("KGDB: refusing system sleep while ARM64 hw breakpoint topology is pinned\n");
		return NOTIFY_BAD;
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block kgdb_hw_pm_nb = {
	.notifier_call = kgdb_hw_pm_notify,
	.priority = INT_MAX,
};

static int kgdb_hw_perf_type(enum kgdb_bptype bptype)
{
	switch (bptype) {
	case BP_HARDWARE_BREAKPOINT:
		return HW_BREAKPOINT_X;
	case BP_WRITE_WATCHPOINT:
		return HW_BREAKPOINT_W;
	case BP_READ_WATCHPOINT:
		return HW_BREAKPOINT_R;
	case BP_ACCESS_WATCHPOINT:
		return HW_BREAKPOINT_RW;
	default:
		return -EINVAL;
	}
}

static int kgdb_hw_normalize_len(unsigned long addr, int len,
				 enum kgdb_bptype bptype)
{
	if (bptype == BP_HARDWARE_BREAKPOINT)
		return (addr & 0x3) ? -EINVAL : AARCH64_INSN_SIZE;

	if (len < HW_BREAKPOINT_LEN_1 || len > HW_BREAKPOINT_LEN_8)
		return -EINVAL;

	/* An AArch64 watchpoint cannot span two aligned 8-byte blocks. */
	if ((addr & 0x7) + len > HW_BREAKPOINT_LEN_8)
		return -EINVAL;

	return len;
}

static int kgdb_hw_prepare_event(struct perf_event *event,
				 unsigned long addr, int len,
				 int perf_type, bool enabled)
{
	event->attr.bp_addr = addr;
	event->attr.bp_len = len;
	event->attr.bp_type = perf_type;
	event->attr.disabled = !enabled;

	return arch_validate_hwbkpt_settings(event);
}

static bool kgdb_hw_all_cpus_quiesced(void)
{
	return cpumask_subset(&kgdb_hw_cpu_mask,
			      &kgdb_hw_quiesced_cpus);
}

static void kgdb_hw_uninstall_cpu(struct kgdb_hw_breakpoint *slot,
				  struct perf_event *event, int cpu)
{
	if (!cpumask_test_cpu(cpu, &slot->installed_cpus))
		return;

	arch_uninstall_hw_breakpoint(event);
	event->attr.disabled = 1;
	event->hw.state = PERF_HES_STOPPED;
	smp_wmb();
	cpumask_clear_cpu(cpu, &slot->installed_cpus);
}

static bool kgdb_hw_release_cpu_reservation(struct kgdb_hw_breakpoint *slot,
					    struct perf_event *event,
					    int cpu)
{
	/* The lockless debugger accounting is only safe after full roundup. */
	if (!kgdb_hw_all_cpus_quiesced())
		return false;
	if (cpumask_test_and_set_cpu(cpu, &slot->releasing_cpus))
		return false;
	if (cpumask_test_cpu(cpu, &slot->installed_cpus))
		goto busy;
	if (!cpumask_test_cpu(cpu, &slot->reserved_cpus))
		goto released;
	if (dbg_release_bp_slot(event)) {
		pr_err("KGDB: failed to release hw breakpoint at %lx on cpu%d\n",
		       slot->addr, cpu);
		goto busy;
	}

	cpumask_clear_cpu(cpu, &slot->reserved_cpus);
released:
	cpumask_clear_cpu(cpu, &slot->releasing_cpus);
	return true;

busy:
	cpumask_clear_cpu(cpu, &slot->releasing_cpus);
	return false;
}

static void kgdb_hw_retire_cpu(struct kgdb_hw_breakpoint *slot,
			       struct perf_event *event, int cpu)
{
	kgdb_hw_uninstall_cpu(slot, event, cpu);
}

static bool kgdb_hw_finish_retire(struct kgdb_hw_breakpoint *slot)
{
	int cpu;

	if (!READ_ONCE(slot->retiring))
		return true;

	for_each_cpu(cpu, &slot->reserved_cpus) {
		struct perf_event **pevent;

		if (cpumask_test_cpu(cpu, &slot->installed_cpus))
			continue;
		pevent = per_cpu_ptr(slot->events, cpu);
		if (*pevent &&
		    !kgdb_hw_release_cpu_reservation(slot, *pevent, cpu))
			return false;
	}

	if (!cpumask_empty(&slot->reserved_cpus) ||
	    !cpumask_empty(&slot->releasing_cpus))
		return false;

	slot->addr = 0;
	slot->len = 0;
	slot->bptype = BP_BREAKPOINT;
	slot->perf_type = HW_BREAKPOINT_EMPTY;
	smp_wmb();
	WRITE_ONCE(slot->retiring, false);
	return true;
}

static int kgdb_hw_reserve_slot(struct kgdb_hw_breakpoint *slot,
				unsigned long addr, int len, int perf_type)
{
	int cpu;
	int ret = 0;

	if (!kgdb_hw_all_cpus_quiesced())
		return -EBUSY;
	if (!cpumask_empty(&slot->reserved_cpus) ||
	    !cpumask_empty(&slot->installed_cpus) ||
	    !cpumask_empty(&slot->releasing_cpus))
		return -EBUSY;

	for_each_cpu(cpu, &kgdb_hw_cpu_mask) {
		struct perf_event **pevent;
		struct perf_event *event;

		pevent = per_cpu_ptr(slot->events, cpu);
		event = *pevent;
		if (!event) {
			ret = -ENODEV;
			goto fail;
		}

		ret = kgdb_hw_prepare_event(event, addr, len, perf_type, false);
		if (ret)
			goto fail;

		ret = dbg_reserve_bp_slot(event);
		if (ret)
			goto fail;
		cpumask_set_cpu(cpu, &slot->reserved_cpus);
	}

	return 0;

fail:
	for_each_cpu(cpu, &slot->reserved_cpus) {
		struct perf_event **pevent = per_cpu_ptr(slot->events, cpu);

		kgdb_hw_release_cpu_reservation(slot, *pevent, cpu);
	}
	if (!cpumask_empty(&slot->reserved_cpus))
		WRITE_ONCE(slot->retiring, true);
	return ret;
}

static int kgdb_hw_release_slot(struct kgdb_hw_breakpoint *slot, bool force)
{
	cpumask_t reserved;

	if (!force && (!kgdb_hw_all_cpus_quiesced() ||
		      !cpumask_empty(&slot->installed_cpus)))
		return -EBUSY;
	cpumask_copy(&reserved, &slot->reserved_cpus);

	/*
	 * A CPU may still be completing the private step for this incarnation.
	 * Retire the global metadata, but leave every CPU's hit state and any
	 * still-installed comparator under that CPU's ownership.
	 */
	slot->retired_generation = READ_ONCE(slot->generation);
	WRITE_ONCE(slot->retiring, true);
	smp_wmb();
	WRITE_ONCE(slot->enabled, false);
	smp_mb();
	if (kgdb_hw_finish_retire(slot))
		return 0;
	if (force)
		return 0;

	/* A locked perf reservation mutex fails before changing any CPU. */
	if (cpumask_equal(&reserved, &slot->reserved_cpus)) {
		WRITE_ONCE(slot->retiring, false);
		smp_wmb();
		WRITE_ONCE(slot->enabled, true);
	}
	return -EBUSY;
}

static int kgdb_set_hw_breakpoint(unsigned long addr, int len,
				  enum kgdb_bptype bptype)
{
	struct kgdb_hw_breakpoint *slot = NULL;
	int perf_type;
	int normalized_len;
	unsigned int i;
	int ret;

	if (!kgdb_hw_slot_count)
		return -ENODEV;
	if (addr < TASK_SIZE)
		return -EINVAL;

	perf_type = kgdb_hw_perf_type(bptype);
	if (perf_type < 0)
		return perf_type;

	normalized_len = kgdb_hw_normalize_len(addr, len, bptype);
	if (normalized_len < 0)
		return normalized_len;

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *candidate = &kgdb_hw_breakpoints[i];

		kgdb_hw_finish_retire(candidate);
		if (candidate->enabled && candidate->addr == addr &&
		    candidate->len == normalized_len &&
		    candidate->bptype == bptype)
			return 0;
		if (!candidate->enabled && !candidate->retiring && !slot)
			slot = candidate;
	}
	if (!slot || !slot->events)
		return -ENOSPC;

	ret = kgdb_hw_reserve_slot(slot, addr, normalized_len, perf_type);
	if (ret)
		return ret;

	/* Publish a complete, new incarnation after all metadata is populated. */
	slot->addr = addr;
	slot->len = normalized_len;
	slot->bptype = bptype;
	slot->perf_type = perf_type;
	slot->generation = atomic64_inc_return(&kgdb_hw_generation);
	smp_wmb();
	WRITE_ONCE(slot->enabled, true);
	return 0;
}

static int kgdb_remove_hw_breakpoint(unsigned long addr, int len,
				     enum kgdb_bptype bptype)
{
	int normalized_len;
	unsigned int i;

	normalized_len = kgdb_hw_normalize_len(addr, len, bptype);
	if (normalized_len < 0)
		return normalized_len;

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];

		if (!slot->enabled || slot->addr != addr ||
		    slot->len != normalized_len || slot->bptype != bptype)
			continue;
		return kgdb_hw_release_slot(slot, false);
	}

	return -ENOENT;
}

static void kgdb_remove_all_hw_breakpoints(void)
{
	unsigned int i;

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		if (kgdb_hw_breakpoints[i].enabled)
			kgdb_hw_release_slot(&kgdb_hw_breakpoints[i], true);
	}
}

static void kgdb_sync_hw_breakpoints(void)
{
	unsigned int i;

	if (!kgdb_hw_all_cpus_quiesced())
		return;
	for (i = 0; i < kgdb_hw_slot_count; i++)
		kgdb_hw_finish_retire(&kgdb_hw_breakpoints[i]);
}

static void kgdb_disable_hw_breakpoints(struct pt_regs *regs)
{
	unsigned int i;
	int cpu = raw_smp_processor_id();

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];
		struct perf_event **pevent;
		struct perf_event *event;

		if (!slot->events)
			continue;
		pevent = per_cpu_ptr(slot->events, cpu);
		event = *pevent;
		if (!event)
			continue;
		kgdb_hw_uninstall_cpu(slot, event, cpu);
	}
	smp_wmb();
	cpumask_set_cpu(cpu, &kgdb_hw_quiesced_cpus);
}

static void kgdb_correct_hw_breakpoints(void)
{
	struct kgdb_hw_cpu_state *state = this_cpu_ptr(&kgdb_hw_cpu_state);
	unsigned int i;
	int cpu = raw_smp_processor_id();

	/* This CPU is leaving the all-stop reservation-accounting barrier. */
	cpumask_clear_cpu(cpu, &kgdb_hw_quiesced_cpus);
	if (state->phase == KGDB_HW_CPU_STEP)
		return;

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];
		struct perf_event **pevent;
		struct perf_event *event;
		int ret;
		u64 generation;

		if (!slot->events)
			continue;
		pevent = per_cpu_ptr(slot->events, cpu);
		event = *pevent;
		if (!event)
			continue;
		if (READ_ONCE(slot->retiring)) {
			kgdb_hw_retire_cpu(slot, event, cpu);
			continue;
		}
		if (!READ_ONCE(slot->enabled) ||
		    !cpumask_test_cpu(cpu, &slot->reserved_cpus) ||
		    cpumask_test_cpu(cpu, &slot->installed_cpus))
			continue;

		generation = READ_ONCE(slot->generation);
		*per_cpu_ptr(slot->armed_generation, cpu) = generation;
		/* Claim local ownership before hardware can be retired remotely. */
		cpumask_set_cpu(cpu, &slot->installed_cpus);
		smp_wmb();
		if (!READ_ONCE(slot->enabled) || READ_ONCE(slot->retiring) ||
		    generation != READ_ONCE(slot->generation)) {
			cpumask_clear_cpu(cpu, &slot->installed_cpus);
			continue;
		}
		ret = kgdb_hw_prepare_event(event, slot->addr, slot->len,
					    slot->perf_type, true);
		if (!ret)
			ret = arch_install_hw_breakpoint(event);
		if (ret) {
			kgdb_hw_prepare_event(event, slot->addr, slot->len,
					      slot->perf_type, false);
			pr_err("KGDB: failed to install hw breakpoint at %lx on cpu%d: %d\n",
			       slot->addr, cpu, ret);
			*per_cpu_ptr(slot->armed_generation, cpu) = 0;
			event->hw.state = PERF_HES_STOPPED;
			cpumask_clear_cpu(cpu, &slot->installed_cpus);
			slot->retired_generation = generation;
			WRITE_ONCE(slot->retiring, true);
			smp_wmb();
			WRITE_ONCE(slot->enabled, false);
		} else {
			event->hw.state = 0;
			smp_wmb();
			if (!READ_ONCE(slot->enabled) ||
			    READ_ONCE(slot->retiring) ||
			    generation != READ_ONCE(slot->generation))
				kgdb_hw_retire_cpu(slot, event, cpu);
		}
	}
}

static void kgdb_hw_overflow_handler(struct perf_event *event,
				     struct perf_sample_data *data,
				     struct pt_regs *regs)
{
	struct kgdb_hw_cpu_state *state = this_cpu_ptr(&kgdb_hw_cpu_state);
	struct kgdb_hw_breakpoint *slot = event->overflow_handler_context;
	int cpu = raw_smp_processor_id();
	u64 armed_generation;

	if (!slot)
		return;
	armed_generation = *this_cpu_ptr(slot->armed_generation);
	if (!READ_ONCE(slot->enabled)) {
		if (READ_ONCE(slot->retiring) &&
		    armed_generation == READ_ONCE(slot->retired_generation)) {
			/* A timed-out CPU owns the final local uninstall. */
			kgdb_hw_retire_cpu(slot, event, cpu);
			return;
		}
		pr_err("KGDB: stale hw breakpoint generation %llu on cpu%d\n",
		       (unsigned long long)armed_generation, cpu);
		arch_uninstall_hw_breakpoint(event);
		event->attr.disabled = 1;
		event->hw.state = PERF_HES_STOPPED;
		cpumask_clear_cpu(cpu, &slot->installed_cpus);
	}
	if ((READ_ONCE(slot->enabled) &&
	     !cpumask_test_cpu(cpu, &slot->installed_cpus)) || user_mode(regs) ||
	    state->phase != KGDB_HW_CPU_IDLE)
		return;

	/* The record must remain valid even if another CPU reuses the slot. */
	state->hit_pc = instruction_pointer(regs);
	state->hit_generation = armed_generation;
	state->hit_from_kgdb_step = kgdb_step_ref_is_owned();
	smp_wmb();
	state->phase = KGDB_HW_CPU_HIT;
	kgdb_handle_exception(1, SIGTRAP, 0, regs);
}

NOKPROBE_SYMBOL(kgdb_hw_overflow_handler);

static bool kgdb_prepare_hw_step(struct pt_regs *regs,
				 enum kgdb_hw_resume_mode resume_mode)
{
	struct kgdb_hw_cpu_state *state = this_cpu_ptr(&kgdb_hw_cpu_state);
	bool active;

	if (state->phase == KGDB_HW_CPU_STEP) {
		state->resume_mode = resume_mode;
		return true;
	}
	if (state->phase != KGDB_HW_CPU_HIT)
		return false;
	if (instruction_pointer(regs) != state->hit_pc) {
		state->phase = KGDB_HW_CPU_IDLE;
		state->hit_generation = 0;
		state->hit_from_kgdb_step = false;
		return false;
	}

	active = kernel_active_single_step();
	state->step_owned = state->hit_from_kgdb_step &&
		kgdb_step_ref_take();
	if (!active) {
		/* Replace a transferred-but-cleared SS without leaking its ref. */
		if (state->step_owned)
			kernel_disable_single_step();
		kernel_enable_single_step(regs);
		state->step_owned = true;
	}
	state->resume_mode = resume_mode;
	state->hit_from_kgdb_step = false;
	state->phase = KGDB_HW_CPU_STEP;
	return true;
}

static enum kgdb_hw_step_action kgdb_finish_hw_step(void)
{
	struct kgdb_hw_cpu_state *state = this_cpu_ptr(&kgdb_hw_cpu_state);
	enum kgdb_hw_resume_mode resume_mode;
	bool owned;

	if (state->phase != KGDB_HW_CPU_STEP)
		return KGDB_HW_STEP_NONE;

	owned = state->step_owned;
	resume_mode = state->resume_mode;
	state->step_owned = false;
	state->resume_mode = KGDB_HW_RESUME_NONE;
	state->hit_generation = 0;
	state->phase = KGDB_HW_CPU_IDLE;
	if (owned)
		kernel_disable_single_step();
	kgdb_correct_hw_breakpoints();

	if (resume_mode == KGDB_HW_RESUME_STEP)
		return KGDB_HW_STEP_REPORT;
	if (resume_mode == KGDB_HW_RESUME_CONTINUE)
		return owned ? KGDB_HW_STEP_CONSUME : KGDB_HW_STEP_PASS;
	return KGDB_HW_STEP_PASS;
}

static void kgdb_hw_late_init(void)
{
	struct perf_event_attr attr;
	int ret;
	unsigned int i;
	unsigned int nr_brps = get_num_brps();
	unsigned int nr_wrps = get_num_wrps();

	if (kgdb_hw_slot_count)
		return;

	/* Preallocated perf events and manual arch installs require a fixed set. */
	lock_system_sleep();
	cpu_hotplug_disable();
	kgdb_hw_hotplug_disabled = true;
	if (!cpumask_equal(cpu_present_mask, cpu_online_mask)) {
		pr_err("KGDB: ARM64 hw breakpoints require every present CPU online\n");
		goto fail_unpin;
	}
	cpumask_copy(&kgdb_hw_cpu_mask, cpu_online_mask);
	cpumask_clear(&kgdb_hw_quiesced_cpus);
	kgdb_hw_slot_count = min_t(unsigned int, nr_brps + nr_wrps,
				   ARRAY_SIZE(kgdb_hw_breakpoints));
	if (!kgdb_hw_slot_count)
		goto fail_unpin;
	hw_breakpoint_init(&attr);
	attr.bp_addr = (unsigned long)kgdb_arch_init;
	attr.bp_len = HW_BREAKPOINT_LEN_1;
	attr.bp_type = HW_BREAKPOINT_W;
	attr.disabled = 1;

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];
		int cpu;

		if (slot->events)
			continue;
		cpumask_clear(&slot->reserved_cpus);
		cpumask_clear(&slot->installed_cpus);
		cpumask_clear(&slot->releasing_cpus);
		WRITE_ONCE(slot->enabled, false);
		WRITE_ONCE(slot->retiring, false);
		slot->armed_generation = alloc_percpu(u64);
		if (!slot->armed_generation) {
			pr_err("KGDB: cannot allocate generation state for hw breakpoint slot %u\n",
			       i);
			goto fail;
		}
		slot->events = register_wide_hw_breakpoint(&attr,
							   kgdb_hw_overflow_handler, slot);
		if (IS_ERR((void * __force)slot->events)) {
			pr_err("KGDB: cannot preallocate ARM64 hw breakpoint slot %u: %ld\n",
			       i, PTR_ERR((void * __force)slot->events));
			slot->events = NULL;
			free_percpu(slot->armed_generation);
			slot->armed_generation = NULL;
			goto fail;
		}

		for_each_cpu(cpu, &kgdb_hw_cpu_mask) {
			struct perf_event **pevent = per_cpu_ptr(slot->events, cpu);
			struct perf_event *event = *pevent;

			event->hw.sample_period = 1;
			event->hw.state = PERF_HES_STOPPED;
			if (event->destroy) {
				event->destroy = NULL;
				release_bp_slot(event);
			}
		}
	}

	ret = register_pm_notifier(&kgdb_hw_pm_nb);
	if (ret) {
		pr_err("KGDB: cannot guard fixed hw breakpoint topology from suspend: %d\n",
		       ret);
		goto fail;
	}
	kgdb_hw_pm_registered = true;
	pr_info("KGDB: ARM64 hardware breakpoints ready (%u BRP, %u WRP, %u CPUs pinned; suspend blocked)\n",
		nr_brps, nr_wrps, cpumask_weight(&kgdb_hw_cpu_mask));
	unlock_system_sleep();
	return;

fail:
	while (i--) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];

		unregister_wide_hw_breakpoint(slot->events);
		slot->events = NULL;
		free_percpu(slot->armed_generation);
		slot->armed_generation = NULL;
	}
	kgdb_hw_slot_count = 0;
fail_unpin:
	cpumask_clear(&kgdb_hw_cpu_mask);
	cpumask_clear(&kgdb_hw_quiesced_cpus);
	if (kgdb_hw_hotplug_disabled) {
		cpu_hotplug_enable();
		kgdb_hw_hotplug_disabled = false;
	}
	unlock_system_sleep();
}

static void kgdb_hw_cleanup_cpu(void *unused)
{
	unsigned int i;
	int cpu = raw_smp_processor_id();

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];
		struct perf_event **pevent;

		if (!slot->events)
			continue;
		pevent = per_cpu_ptr(slot->events, cpu);
		if (*pevent)
			kgdb_hw_uninstall_cpu(slot, *pevent, cpu);
	}
}

static void kgdb_hw_cleanup(void)
{
	unsigned int i;
	int cpu;

	lock_system_sleep();
	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];

		WRITE_ONCE(slot->retiring, true);
		smp_wmb();
		WRITE_ONCE(slot->enabled, false);
	}
	on_each_cpu(kgdb_hw_cleanup_cpu, NULL, 1);

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		if (!cpumask_empty(&kgdb_hw_breakpoints[i].installed_cpus)) {
			pr_err("KGDB: retaining hw breakpoint storage with offline comparators\n");
			unlock_system_sleep();
			return;
		}
	}

	for (i = 0; i < kgdb_hw_slot_count; i++) {
		struct kgdb_hw_breakpoint *slot = &kgdb_hw_breakpoints[i];

		if (!slot->events)
			continue;
		for_each_cpu(cpu, &slot->reserved_cpus) {
			struct perf_event **pevent = per_cpu_ptr(slot->events, cpu);

			release_bp_slot(*pevent);
			cpumask_clear_cpu(cpu, &slot->reserved_cpus);
		}
		unregister_wide_hw_breakpoint(slot->events);
		slot->events = NULL;
		free_percpu(slot->armed_generation);
		slot->armed_generation = NULL;
		WRITE_ONCE(slot->retiring, false);
	}
	cpumask_clear(&kgdb_hw_quiesced_cpus);
	cpumask_clear(&kgdb_hw_cpu_mask);
	kgdb_hw_slot_count = 0;
	if (kgdb_hw_pm_registered) {
		unregister_pm_notifier(&kgdb_hw_pm_nb);
		kgdb_hw_pm_registered = false;
	}
	if (kgdb_hw_hotplug_disabled) {
		cpu_hotplug_enable();
		kgdb_hw_hotplug_disabled = false;
	}
	unlock_system_sleep();
}
#else
static inline bool kgdb_prepare_hw_step(struct pt_regs *regs,
					enum kgdb_hw_resume_mode resume_mode)
{
	return false;
}

static inline enum kgdb_hw_step_action kgdb_finish_hw_step(void)
{
	return KGDB_HW_STEP_NONE;
}

static inline void kgdb_hw_late_init(void)
{
}

static inline void kgdb_hw_cleanup(void)
{
}
#endif

static int __init kgdb_hw_post_smp_init(void)
{
	mutex_lock(&kgdb_hw_init_lock);
	kgdb_hw_smp_ready = true;
	if (kgdb_hw_init_requested)
		kgdb_hw_late_init();
	mutex_unlock(&kgdb_hw_init_lock);
	return 0;
}
subsys_initcall(kgdb_hw_post_smp_init);

static void kgdb_arch_update_addr(struct pt_regs *regs,
				char *remcom_in_buffer)
{
	unsigned long addr;
	char *ptr;

	ptr = &remcom_in_buffer[1];
	if (kgdb_hex2long(&ptr, &addr))
		kgdb_arch_set_pc(regs, addr);
	else if (compiled_break == 1)
		kgdb_arch_set_pc(regs, regs->pc + 4);

	compiled_break = 0;
}

int kgdb_arch_handle_exception(int exception_vector, int signo,
			       int err_code, char *remcom_in_buffer,
			       char *remcom_out_buffer,
			       struct pt_regs *linux_regs)
{
	int err;

	switch (remcom_in_buffer[0]) {
	case 'D':
	case 'k':
		/*
		 * Packet D (Detach), k (kill). No special handling
		 * is required here. Handle same as c packet.
		 */
	case 'c':
		/*
		 * Packet c (Continue) to continue executing.
		 * Set pc to required address.
		 * Try to read optional parameter and set pc.
		 * If this was a compiled breakpoint, we need to move
		 * to the next instruction else we will just breakpoint
		 * over and over again.
		 */
		kgdb_arch_update_addr(linux_regs, remcom_in_buffer);
		atomic_set(&kgdb_cpu_doing_single_step, -1);
		kgdb_single_step =  0;

		/*
		 * Received continue command, disable single step
		 */
		if (!kgdb_prepare_hw_step(linux_regs,
					  KGDB_HW_RESUME_CONTINUE))
			kgdb_step_ref_put();

		err = 0;
		break;
	case 's':
		/*
		 * Update step address value with address passed
		 * with step packet.
		 * On debug exception return PC is copied to ELR
		 * So just update PC.
		 * If no step address is passed, resume from the address
		 * pointed by PC. Do not update PC
		 */
		kgdb_arch_update_addr(linux_regs, remcom_in_buffer);
		atomic_set(&kgdb_cpu_doing_single_step, raw_smp_processor_id());
		kgdb_single_step =  1;

		/*
		 * Enable single step handling
		 */
		if (!kgdb_prepare_hw_step(linux_regs, KGDB_HW_RESUME_STEP)) {
			kgdb_step_ref_put();
			if (!kernel_active_single_step())
				kgdb_step_ref_get(linux_regs);
		}
		err = 0;
		break;
	default:
		err = -1;
	}
	return err;
}

static int kgdb_brk_fn(struct pt_regs *regs, unsigned int esr)
{
	if (user_mode(regs))
		return DBG_HOOK_ERROR;

	kgdb_handle_exception(1, SIGTRAP, 0, regs);
	return DBG_HOOK_HANDLED;
}
NOKPROBE_SYMBOL(kgdb_brk_fn)

static int kgdb_compiled_brk_fn(struct pt_regs *regs, unsigned int esr)
{
	if (user_mode(regs))
		return DBG_HOOK_ERROR;

	compiled_break = 1;
	kgdb_handle_exception(1, SIGTRAP, 0, regs);

	return DBG_HOOK_HANDLED;
}
NOKPROBE_SYMBOL(kgdb_compiled_brk_fn);

static int kgdb_step_brk_fn(struct pt_regs *regs, unsigned int esr)
{
	enum kgdb_hw_step_action action;

	if (user_mode(regs))
		return DBG_HOOK_ERROR;

	action = kgdb_finish_hw_step();
	if (action == KGDB_HW_STEP_CONSUME)
		return DBG_HOOK_HANDLED;
	if (action == KGDB_HW_STEP_PASS)
		return DBG_HOOK_ERROR;
	if (action == KGDB_HW_STEP_REPORT) {
		/* Restore the core handoff state a competing master may change. */
		atomic_set(&kgdb_cpu_doing_single_step, raw_smp_processor_id());
		kgdb_single_step = 1;
		kgdb_handle_exception(1, SIGTRAP, 0, regs);
		return DBG_HOOK_HANDLED;
	}

	if (!kgdb_single_step)
		return DBG_HOOK_ERROR;

	kgdb_step_ref_put();
	kgdb_handle_exception(1, SIGTRAP, 0, regs);
	return DBG_HOOK_HANDLED;
}
NOKPROBE_SYMBOL(kgdb_step_brk_fn);

static struct break_hook kgdb_brkpt_hook = {
	.esr_mask	= 0xffffffff,
	.esr_val	= (u32)ESR_ELx_VAL_BRK64(KGDB_DYN_DBG_BRK_IMM),
	.fn		= kgdb_brk_fn
};

static struct break_hook kgdb_compiled_brkpt_hook = {
	.esr_mask	= 0xffffffff,
	.esr_val	= (u32)ESR_ELx_VAL_BRK64(KGDB_COMPILED_DBG_BRK_IMM),
	.fn		= kgdb_compiled_brk_fn
};

static struct step_hook kgdb_step_hook = {
	.fn			= kgdb_step_brk_fn,
	.notify_after_handler	= true,
};

static void kgdb_call_nmi_hook(void *ignored)
{
	kgdb_nmicallback(raw_smp_processor_id(), get_irq_regs());
}

void kgdb_roundup_cpus(unsigned long flags)
{
	local_irq_enable();
	smp_call_function(kgdb_call_nmi_hook, NULL, 0);
	local_irq_disable();
}

static int __kgdb_notify(struct die_args *args, unsigned long cmd)
{
	struct pt_regs *regs = args->regs;

	if (kgdb_handle_exception(1, args->signr, cmd, regs))
		return NOTIFY_DONE;
	return NOTIFY_STOP;
}

static int
kgdb_notify(struct notifier_block *self, unsigned long cmd, void *ptr)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __kgdb_notify(ptr, cmd);
	local_irq_restore(flags);

	return ret;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call	= kgdb_notify,
	/*
	 * Want to be lowest priority
	 */
	.priority	= -INT_MAX,
};

/*
 * kgdb_arch_init - Perform any architecture specific initialization.
 * This function will handle the initialization of any architecture
 * specific callbacks.
 */
int kgdb_arch_init(void)
{
	int ret = register_die_notifier(&kgdb_notifier);

	if (ret != 0)
		return ret;

	register_break_hook(&kgdb_brkpt_hook);
	register_break_hook(&kgdb_compiled_brkpt_hook);
	register_step_hook(&kgdb_step_hook);
	return 0;
}

/*
 * kgdb_arch_exit - Perform any architecture specific uninitalization.
 * This function will handle the uninitalization of any architecture
 * specific callbacks, for dynamic registration and unregistration.
 */
void kgdb_arch_exit(void)
{
	mutex_lock(&kgdb_hw_init_lock);
	kgdb_hw_init_requested = false;
	kgdb_hw_cleanup();
	mutex_unlock(&kgdb_hw_init_lock);
	unregister_break_hook(&kgdb_brkpt_hook);
	unregister_break_hook(&kgdb_compiled_brkpt_hook);
	unregister_step_hook(&kgdb_step_hook);
	unregister_die_notifier(&kgdb_notifier);
}

struct kgdb_arch arch_kgdb_ops = {
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	.flags			= KGDB_HW_BREAKPOINT,
	.set_hw_breakpoint	= kgdb_set_hw_breakpoint,
	.remove_hw_breakpoint	= kgdb_remove_hw_breakpoint,
	.disable_hw_break	= kgdb_disable_hw_breakpoints,
	.remove_all_hw_break	= kgdb_remove_all_hw_breakpoints,
	.sync_hw_break		= kgdb_sync_hw_breakpoints,
	.correct_hw_break	= kgdb_correct_hw_breakpoints,
#endif
};

int kgdb_arch_set_breakpoint(struct kgdb_bkpt *bpt)
{
	int err;

	BUILD_BUG_ON(AARCH64_INSN_SIZE != BREAK_INSTR_SIZE);

	err = aarch64_insn_read((void *)bpt->bpt_addr, (u32 *)bpt->saved_instr);
	if (err)
		return err;

	return aarch64_insn_write((void *)bpt->bpt_addr,
			(u32)AARCH64_BREAK_KGDB_DYN_DBG);
}

int kgdb_arch_remove_breakpoint(struct kgdb_bkpt *bpt)
{
	return aarch64_insn_write((void *)bpt->bpt_addr,
			*(u32 *)bpt->saved_instr);
}

void kgdb_arch_late(void)
{
	mutex_lock(&kgdb_hw_init_lock);
	kgdb_hw_init_requested = true;
	if (kgdb_hw_smp_ready)
		kgdb_hw_late_init();
	mutex_unlock(&kgdb_hw_init_lock);
}
