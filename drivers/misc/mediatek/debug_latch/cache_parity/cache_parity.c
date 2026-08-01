/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Mars.Cheng <mars.cheng@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/printk.h>
#include <linux/bug.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqreturn.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/cpuhotplug.h>
#include <linux/sched/clock.h>
#include <mt-plat/aee.h>
#include <cache_parity.h>
#include <asm/cputype.h>
#include <linux/irqchip/mtk-gic-extend.h>

/*
 * internal weak function, by-chip implemented.
 */
void __attribute__((weak)) ecc_dump_debug_info(void)
{
	pr_notice("%s is not implemented\n", __func__);
}

#define ECC_LOG(fmt, ...) \
	do { \
		pr_notice(fmt, __VA_ARGS__); \
		aee_sram_printk(fmt, __VA_ARGS__); \
	} while (0)

static DEFINE_SPINLOCK(parity_isr_lock);

void __iomem *parity_debug_base;
static unsigned int err_level;
static unsigned int irq_count;
static unsigned int version;
static struct parity_irq_record_t *parity_irq_record;
static bool cache_error_happened;
static unsigned int cache_error_times;
static u64 cache_error_timestamp;
static DEFINE_SPINLOCK(cache_parity_status_lock);

#define CACHE_PARITY_EVENT_DEPTH	32
#define CACHE_PARITY_REPORT_BATCH	CACHE_PARITY_EVENT_DEPTH
#define CACHE_PARITY_DRAIN_LIMIT	4
#define CACHE_PARITY_SPURIOUS_LIMIT	3
#define CACHE_PARITY_ACTION_DRAIN_LIMIT	BIT(0)
#define CACHE_PARITY_ACTION_CFI_DISABLED	BIT(1)
#define CACHE_PARITY_ACTION_IRQ_DISABLED	BIT(2)
#define CACHE_PARITY_ACTION_SPURIOUS	BIT(3)
#define CACHE_PARITY_ACTION_ERRATUM	BIT(4)
#define ERXSTATUS_AV_BIT		BIT_ULL(31)
#define ERXSTATUS_V_BIT		BIT_ULL(30)
#define ERXSTATUS_ER_BIT		BIT_ULL(28)
#define ERXSTATUS_OF_BIT		BIT_ULL(27)
#define ERXSTATUS_MV_BIT		BIT_ULL(26)
#define ERXSTATUS_CE_MASK	GENMASK_ULL(25, 24)
#define ERXSTATUS_PN_BIT		BIT_ULL(22)
#define ERXSTATUS_UET_MASK	GENMASK_ULL(21, 20)
#define ERXSTATUS_CI_BIT		BIT_ULL(19)
#define ERXFR_CFI_MASK		GENMASK_ULL(11, 10)
#define ERXFR_CFI_SHARED		(0x2ULL << 10)
#define ERXFR_CFI_SPLIT		(0x3ULL << 10)
#define ERXCTLR_CFI_BIT		BIT_ULL(8)
#define ERXCTLR_WCFI_BIT		BIT_ULL(9)
#define ERXSTATUS_SINGLE_W1C_MASK	(ERXSTATUS_AV_BIT | ERXSTATUS_V_BIT | \
				 ECC_UE_BIT | ERXSTATUS_ER_BIT | \
				 ERXSTATUS_OF_BIT | ERXSTATUS_MV_BIT | \
				 ECC_DE_BIT | ERXSTATUS_PN_BIT | \
				 ERXSTATUS_CI_BIT)

struct cache_parity_event_v2 {
	int irq;
	u32 hwirq;
	u32 cpu;
	u32 source_count;
	u32 actions;
	u64 misc0_el1;
	u64 addr_el1;
	u64 status_el1;
	u64 timestamp;
};

struct cache_parity_irq_data {
	int irq;
	u32 hwirq;
	unsigned long status_flags;
	atomic_t error_count;
	atomic_t spurious_count;
	atomic_t irq_disabled;
	atomic_t hotplug_disabled;
};

struct cache_parity_v2_data {
	struct hlist_node cpuhp_node;
	unsigned int core_count;
	struct cache_parity_irq_data *irqs;
};

static int cache_parity_cpuhp_state = -1;

static struct {
	struct work_struct work;
	spinlock_t lock; /* Protects all queue fields below. */
	unsigned int head;
	unsigned int tail;
	unsigned int count;
	unsigned int dropped;
	bool fatal_pending;
	struct cache_parity_event_v2 fatal_event;
	struct cache_parity_event_v2 events[CACHE_PARITY_EVENT_DEPTH];
} cache_parity_v2_queue = {
	.lock = __SPIN_LOCK_UNLOCKED(cache_parity_v2_queue.lock),
};

static irqreturn_t (*custom_parity_isr)(int irq, void *dev_id);
static int cache_parity_probe(struct platform_device *pdev);
static int cache_parity_remove(struct platform_device *pdev);

static const struct of_device_id cache_parity_of_ids[] = {
	{   .compatible = "mediatek,cache_parity", },
	{}
};

static struct platform_driver cache_parity_drv = {
	.driver = {
		.name = "cache_parity",
		.bus = &platform_bus_type,
		.owner = THIS_MODULE,
		.of_match_table = cache_parity_of_ids,
	},
	.probe = cache_parity_probe,
	.remove = cache_parity_remove,
};

static struct cache_parity_work_data {
	struct work_struct work;
	u32 irq_index;
	u32 status;
} cache_parity_wd;

static void cache_parity_note_error(u64 timestamp)
{
	unsigned long flags;

	spin_lock_irqsave(&cache_parity_status_lock, flags);
	cache_error_happened = true;
	cache_error_times++;
	cache_error_timestamp = timestamp;
	spin_unlock_irqrestore(&cache_parity_status_lock, flags);
}

static ssize_t cache_status_show(struct device_driver *driver,
				 char *buf)
{
	unsigned long flags;
	unsigned int error_times;
	u64 timestamp;
	bool happened;

	spin_lock_irqsave(&cache_parity_status_lock, flags);
	happened = cache_error_happened;
	error_times = cache_error_times;
	timestamp = cache_error_timestamp;
	spin_unlock_irqrestore(&cache_parity_status_lock, flags);

	if (happened)
		return snprintf(buf, PAGE_SIZE, "True, %u times (%llu ns)\n",
				error_times, (unsigned long long)timestamp);
	else
		return snprintf(buf, PAGE_SIZE, "False\n");
}

static DRIVER_ATTR_RO(cache_status);

#ifdef CONFIG_ARM64
static u64 read_ERXMISC0_EL1(void)
{
	u64 v;

	__asm__ volatile ("mrs %0, s3_0_c5_c5_0" : "=r" (v));

	return v;
}

static u64 read_ERXSTATUS_EL1(void)
{
	u64 v;

	__asm__ volatile ("mrs %0, s3_0_c5_c4_2" : "=r" (v));

	return v;
}

static u64 read_ERXADDR_EL1(void)
{
	u64 v;

	__asm__ volatile ("mrs %0, s3_0_c5_c4_3" : "=r" (v));

	return v;
}

static u64 read_ERXFR_EL1(void)
{
	u64 v;

	__asm__ volatile ("mrs %0, s3_0_c5_c4_0" : "=r" (v));

	return v;
}

static u64 read_ERXCTLR_EL1(void)
{
	u64 v;

	__asm__ volatile ("mrs %0, s3_0_c5_c4_1" : "=r" (v));

	return v;
}

static void write_ERXCTLR_EL1(u64 v)
{
	__asm__ volatile ("msr s3_0_c5_c4_1, %0" : : "r" (v));
}

static void write_ERXSTATUS_EL1(u64 v)
{
	__asm__ volatile ("msr s3_0_c5_c4_2, %0" : : "r" (v));
}

static void write_ERXSELR_EL1(u32 v)
{
	__asm__ volatile ("msr s3_0_c5_c3_1, %0" : : "r" (v));
}
#else
/* TODO: aarch32, TBD */
static u64 read_ERXMISC0_EL1(void)
{
	return 0;
}

static u64 read_ERXSTATUS_EL1(void)
{
	return 0;
}

static u64 read_ERXADDR_EL1(void)
{
	return 0;
}

static u64 read_ERXFR_EL1(void)
{
	return 0;
}

static u64 read_ERXCTLR_EL1(void)
{
	return 0;
}

static void write_ERXCTLR_EL1(u64 v)
{
}

static void write_ERXSTATUS_EL1(u64 v)
{
}

static void write_ERXSELR_EL1(u32 v)
{
}
#endif

static void handle_error(struct work_struct *w)
{
	struct cache_parity_work_data *wd;

	wd = container_of(w, struct cache_parity_work_data, work);

	aee_kernel_exception("cache parity",
			     "cache parity error,%s:%d,%s:0x%x\n\n%s\n",
			     "irq_index", wd->irq_index,
			     "status", wd->status,
			     "CRDISPATCH_KEY:Cache Parity Issue");
}

static int cache_parity_event_severity(u64 status_el1)
{
	if (status_el1 & ECC_UE_BIT) {
		switch ((status_el1 & ERXSTATUS_UET_MASK) >> 20) {
		case 0:
			return 7; /* Uncontainable. */
		case 1:
			return 6; /* Unrecoverable. */
		case 3:
			return 5; /* Recoverable. */
		case 2:
			return 4; /* Restartable or latent. */
		}
	}
	if (status_el1 & ECC_DE_BIT)
		return 3;
	if (status_el1 & ECC_CE_BIT)
		return 2;
	return 0;
}

/*
 * CE and UET are read/write-ones-to-clear fields.  Copying a nonzero value
 * other than all ones makes the field UNKNOWN, so build the write value one
 * field at a time and leave the read/write IERR/SERR syndrome fields zero.
 */
static u64 cache_parity_status_clear_value(u64 status_el1)
{
	u64 clear = status_el1 & ERXSTATUS_SINGLE_W1C_MASK;

	if (status_el1 & ERXSTATUS_CE_MASK)
		clear |= ERXSTATUS_CE_MASK;
	if (status_el1 & ERXSTATUS_UET_MASK)
		clear |= ERXSTATUS_UET_MASK;

	return clear;
}

static bool cache_parity_corrected_only(u64 status_el1)
{
	return (status_el1 & ERXSTATUS_CE_MASK) &&
		!(status_el1 & (ECC_UE_BIT | ECC_DE_BIT));
}

/* Suppress only corrected-error fault interrupts; keep FI/UI/ED untouched. */
static bool cache_parity_disable_cfi(void)
{
	u64 cfi = read_ERXFR_EL1() & ERXFR_CFI_MASK;
	u64 clear_mask;
	u64 ctlr;

	if (cfi == ERXFR_CFI_SHARED)
		clear_mask = ERXCTLR_CFI_BIT;
	else if (cfi == ERXFR_CFI_SPLIT)
		clear_mask = ERXCTLR_CFI_BIT | ERXCTLR_WCFI_BIT;
	else
		return false;

	ctlr = read_ERXCTLR_EL1();
	if (!(ctlr & clear_mask))
		return true;

	write_ERXCTLR_EL1(ctlr & ~clear_mask);
	dsb(sy);
	isb();

	return !(read_ERXCTLR_EL1() & clear_mask);
}

static bool cache_parity_disable_source_irq(
	int irq, struct cache_parity_irq_data *irq_data)
{
	if (!irq_data || atomic_cmpxchg(&irq_data->irq_disabled, 0, 1))
		return false;

	disable_irq_nosync(irq);
	return true;
}

static const char *cache_parity_event_type(u64 status_el1)
{
	switch (cache_parity_event_severity(status_el1)) {
	case 7:
		return "UE/UC";
	case 6:
		return "UE/UEU";
	case 5:
		return "UE/UER";
	case 4:
		return "UE/UEO";
	case 3:
		return "DE";
	case 2:
		return "CE";
	default:
		return "NA";
	}
}

static bool cache_parity_dequeue_v2(struct cache_parity_event_v2 *event,
				    unsigned int *dropped)
{
	unsigned long flags;
	bool have_event = false;

	spin_lock_irqsave(&cache_parity_v2_queue.lock, flags);
	if (cache_parity_v2_queue.fatal_pending) {
		*event = cache_parity_v2_queue.fatal_event;
		cache_parity_v2_queue.fatal_pending = false;
		have_event = true;
	} else if (cache_parity_v2_queue.count) {
		*event = cache_parity_v2_queue.events[cache_parity_v2_queue.tail];
		cache_parity_v2_queue.tail =
			(cache_parity_v2_queue.tail + 1) % CACHE_PARITY_EVENT_DEPTH;
		cache_parity_v2_queue.count--;
		have_event = true;
	}
	*dropped = cache_parity_v2_queue.dropped;
	cache_parity_v2_queue.dropped = 0;
	spin_unlock_irqrestore(&cache_parity_v2_queue.lock, flags);

	return have_event;
}

static bool cache_parity_has_pending_v2(void)
{
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&cache_parity_v2_queue.lock, flags);
	pending = cache_parity_v2_queue.fatal_pending ||
		cache_parity_v2_queue.count;
	spin_unlock_irqrestore(&cache_parity_v2_queue.lock, flags);

	return pending;
}

static void cache_parity_reset_queue_v2(void)
{
	unsigned long flags;

	spin_lock_irqsave(&cache_parity_v2_queue.lock, flags);
	cache_parity_v2_queue.head = 0;
	cache_parity_v2_queue.tail = 0;
	cache_parity_v2_queue.count = 0;
	cache_parity_v2_queue.dropped = 0;
	cache_parity_v2_queue.fatal_pending = false;
	spin_unlock_irqrestore(&cache_parity_v2_queue.lock, flags);
}

static void handle_error_v2(struct work_struct *w)
{
	struct cache_parity_event_v2 event;
	struct cache_parity_event_v2 report_event = { };
	unsigned int dropped;
	unsigned int dropped_total = 0;
	unsigned int processed = 0;
	int report_severity = -1;

	(void)w;

	while (processed < CACHE_PARITY_REPORT_BATCH &&
	       cache_parity_dequeue_v2(&event, &dropped)) {
		int severity = cache_parity_event_severity(event.status_el1);

		processed++;
		dropped_total += dropped;
		ECC_LOG("ecc event(%s), cpu:%u, irq:%d, hwirq:%u, "
			 "source_count:%u, actions:0x%x, time_ns:%llu, "
			 "misc0_el1:0x%016llx, "
			 "addr_el1:0x%016llx, status_el1:0x%016llx\n",
			 cache_parity_event_type(event.status_el1), event.cpu,
			 event.irq, event.hwirq, event.source_count,
			 event.actions, (unsigned long long)event.timestamp,
			 (unsigned long long)event.misc0_el1,
			 (unsigned long long)event.addr_el1,
			 (unsigned long long)event.status_el1);

		if (severity > report_severity) {
			report_event = event;
			report_severity = severity;
		}

		/* Do not hold a fatal report behind a batch of corrected errors. */
		if (severity >= 3)
			break;
	}

	if (dropped_total)
		ECC_LOG("dropped %u cache parity event(s): queue full\n",
			dropped_total);

	if (report_severity < 0)
		return;

	/* Platform-specific dumps and AEE reporting must not delay nFAULTIRQ. */
	ecc_dump_debug_info();

	if (report_severity >= 3) {
		aee_kernel_exception("cache parity",
			"ecc error(%s), cpu:%u, irq_index:%u, actions:0x%x, "
			"misc0_el1:%016llx, addr_el1:%016llx, "
			"status_el1:%016llx\n\n%s\n",
			cache_parity_event_type(report_event.status_el1),
			report_event.cpu, report_event.hwirq,
			report_event.actions,
			(unsigned long long)report_event.misc0_el1,
			(unsigned long long)report_event.addr_el1,
			(unsigned long long)report_event.status_el1,
			"CRDISPATCH_KEY:Cache Parity Issue");
	} else {
		aee_kernel_warning("cache parity",
				   "ecc error(%s), cpu:%u, irq_index:%u, actions:0x%x, "
				   "misc0_el1:%016llx, addr_el1:%016llx, "
				   "status_el1:%016llx\n\n%s\n",
				   cache_parity_event_type(report_event.status_el1),
				   report_event.cpu, report_event.hwirq,
				   report_event.actions,
				   (unsigned long long)report_event.misc0_el1,
				   (unsigned long long)report_event.addr_el1,
				   (unsigned long long)report_event.status_el1,
				   "CRDISPATCH_KEY:Cache Parity Issue");
	}

	if (cache_parity_has_pending_v2())
		schedule_work(&cache_parity_v2_queue.work);
}

static bool cache_parity_enqueue_v2(const struct cache_parity_event_v2 *event)
{
	unsigned long flags;
	bool fatal;
	bool queued = false;

	fatal = event->status_el1 & (ECC_UE_BIT | ECC_DE_BIT);

	spin_lock_irqsave(&cache_parity_v2_queue.lock, flags);
	if (fatal) {
		if (cache_parity_v2_queue.fatal_pending) {
			u32 actions = cache_parity_v2_queue.fatal_event.actions |
				event->actions;

			cache_parity_v2_queue.dropped++;
			if (cache_parity_event_severity(event->status_el1) >
			    cache_parity_event_severity(
				cache_parity_v2_queue.fatal_event.status_el1))
				cache_parity_v2_queue.fatal_event = *event;
			cache_parity_v2_queue.fatal_event.actions = actions;
		} else {
			cache_parity_v2_queue.fatal_event = *event;
		}
		cache_parity_v2_queue.fatal_pending = true;
		queued = true;
	} else if (cache_parity_v2_queue.count < CACHE_PARITY_EVENT_DEPTH) {
		cache_parity_v2_queue.events[cache_parity_v2_queue.head] = *event;
		cache_parity_v2_queue.head =
			(cache_parity_v2_queue.head + 1) % CACHE_PARITY_EVENT_DEPTH;
		cache_parity_v2_queue.count++;
		queued = true;
	} else {
		cache_parity_v2_queue.dropped++;
	}
	spin_unlock_irqrestore(&cache_parity_v2_queue.lock, flags);

	return queued;
}

static void cache_parity_kick_v2(bool queued)
{
	if (queued)
		schedule_work(&cache_parity_v2_queue.work);
}

static void cache_parity_capture_v2(struct cache_parity_event_v2 *event,
				    int irq, u32 hwirq,
				    struct cache_parity_irq_data *irq_data,
				    u64 status_el1, u32 actions)
{
	*event = (struct cache_parity_event_v2) {
		.irq = irq,
		.hwirq = hwirq,
		.cpu = raw_smp_processor_id(),
		.actions = actions,
		.status_el1 = status_el1,
	};

	if (status_el1 & ERXSTATUS_MV_BIT)
		event->misc0_el1 = read_ERXMISC0_EL1();
	if (status_el1 & ERXSTATUS_AV_BIT)
		event->addr_el1 = read_ERXADDR_EL1();

	event->timestamp = local_clock();
	if (irq_data)
		event->source_count =
			atomic_inc_return(&irq_data->error_count);
	cache_parity_note_error(event->timestamp);
}

static u64 cache_parity_clear_status_v2(u64 status_el1)
{
	write_ERXSTATUS_EL1(cache_parity_status_clear_value(status_el1));
	dsb(sy);
	isb();

	return read_ERXSTATUS_EL1();
}

static irqreturn_t default_parity_isr_v2(int irq, void *dev_id)
{
#ifdef CONFIG_ARM64_ERRATUM_1800710
	static const struct midr_range erratum_1800710_cpu_list[] = {
		_MIDR_ALL_VERSIONS(MIDR_CORTEX_A76),
		_MIDR_ALL_VERSIONS(MIDR_CORTEX_A77),
	};
#endif

	struct cache_parity_irq_data *irq_data = dev_id;
	u32 hwirq = irq_data ? irq_data->hwirq : virq_to_hwirq(irq);
	u64 status_el1;
	unsigned int attempt;
	bool queued = false;

	/* ERRSELR is PE-local; record 1 is the cluster DSU record. */
	write_ERXSELR_EL1(hwirq == FAULTIRQ_START ? 1 : 0);
	isb();
	status_el1 = read_ERXSTATUS_EL1();

	if (!(status_el1 & ERXSTATUS_V_BIT)) {
		if (irq_data) {
			int spurious =
				atomic_inc_return(&irq_data->spurious_count);

			if (spurious >= CACHE_PARITY_SPURIOUS_LIMIT) {
				struct cache_parity_event_v2 event = {
					.irq = irq,
					.hwirq = hwirq,
					.cpu = raw_smp_processor_id(),
					.source_count = spurious,
					.actions = CACHE_PARITY_ACTION_SPURIOUS,
					.timestamp = local_clock(),
				};

				if (cache_parity_disable_source_irq(irq, irq_data))
					event.actions |=
						CACHE_PARITY_ACTION_IRQ_DISABLED;
				queued = cache_parity_enqueue_v2(&event);
			}
		}
		cache_parity_kick_v2(queued);
		return IRQ_HANDLED;
	}

	if (irq_data)
		atomic_set(&irq_data->spurious_count, 0);

	for (attempt = 0;
	     attempt < CACHE_PARITY_DRAIN_LIMIT &&
		(status_el1 & ERXSTATUS_V_BIT);
	     attempt++) {
		struct cache_parity_event_v2 event;

		cache_parity_capture_v2(&event, irq, hwirq, irq_data,
					status_el1, 0);
	#ifdef CONFIG_ARM64_ERRATUM_1800710
		if (is_midr_in_range_list(read_cpuid_id(),
					  erratum_1800710_cpu_list) &&
		    (status_el1 & ECC_CE_BIT) == (0x2ULL << 24) &&
		    (status_el1 & ECC_SERR_BIT) == 0x2)
			event.actions |= CACHE_PARITY_ACTION_ERRATUM;
	#endif
		if (cache_parity_enqueue_v2(&event))
			queued = true;

		/* Snapshot first; only then acknowledge and verify the record. */
		status_el1 = cache_parity_clear_status_v2(status_el1);
	}

	/*
	 * A continuously replenished level IRQ must not monopolize hardirq
	 * context.  Preserve and acknowledge two more records, suppress only CE
	 * notification if supported, then quarantine a source that is still
	 * asserted.  Fatal interrupt controls are never silently disabled.
	 */
	for (attempt = 0; attempt < 2 && (status_el1 & ERXSTATUS_V_BIT);
	     attempt++) {
		struct cache_parity_event_v2 event;
		u32 actions = CACHE_PARITY_ACTION_DRAIN_LIMIT;

		if (cache_parity_corrected_only(status_el1) &&
		    cache_parity_disable_cfi())
			actions |= CACHE_PARITY_ACTION_CFI_DISABLED;
		cache_parity_capture_v2(&event, irq, hwirq, irq_data,
					status_el1, actions);
		if (cache_parity_enqueue_v2(&event))
			queued = true;
		status_el1 = cache_parity_clear_status_v2(status_el1);
	}

	if (status_el1 & ERXSTATUS_V_BIT) {
		struct cache_parity_event_v2 event;
		u32 actions = CACHE_PARITY_ACTION_DRAIN_LIMIT;

		if (cache_parity_corrected_only(status_el1) &&
		    cache_parity_disable_cfi())
			actions |= CACHE_PARITY_ACTION_CFI_DISABLED;
		if (cache_parity_disable_source_irq(irq, irq_data))
			actions |= CACHE_PARITY_ACTION_IRQ_DISABLED;
		cache_parity_capture_v2(&event, irq, hwirq, irq_data,
					status_el1, actions);
		if (cache_parity_enqueue_v2(&event))
			queued = true;
	}

	/* No worker can touch UART/AEE until all status writes are complete. */
	cache_parity_kick_v2(queued);
	return IRQ_HANDLED;
}

static irqreturn_t default_parity_isr_v1(int irq, void *dev_id)
{
	struct parity_record_t *parity_record;
	unsigned int status;
	unsigned int offset;
	unsigned int irq_idx;
	unsigned int i;

	cache_parity_note_error(local_clock());

	for (i = 0, parity_record = NULL; i < irq_count; i++) {
		if (parity_irq_record[i].irq == irq) {
			irq_idx = i;
			parity_record = &(parity_irq_record[i].parity_record);
			pr_info("parity isr for %d\n", i);
			break;
		}
	}

	if (parity_record == NULL) {
		pr_info("no matched irq %d\n", irq);
		return IRQ_HANDLED;
	}

	status = readl(parity_debug_base + parity_record->check_offset);
	pr_info("status 0x%x\n", status);

	if (status & parity_record->check_mask)
		pr_info("detect cache parity error\n");
	else
		pr_info("no cache parity error\n");

	for (i = 0; i < parity_record->dump_length; i += 4) {
		offset = parity_record->dump_offset + i;
		pr_info("offset 0x%x, val 0x%x\n", offset,
			readl(parity_debug_base + offset));
	}

#ifdef CONFIG_MTK_ENG_BUILD
	WARN_ON(1);
#else
	if (err_level) {
		cache_parity_wd.irq_index = irq_idx;
		cache_parity_wd.status = status;
		schedule_work(&cache_parity_wd.work);
	} else
		WARN_ON(1);
#endif

	spin_lock(&parity_isr_lock);

	if (parity_record->clear_mask) {
		writel(parity_record->clear_mask,
			parity_debug_base + parity_record->clear_offset);
		dsb(sy);
		writel(0x0,
			parity_debug_base + parity_record->clear_offset);
		dsb(sy);

		while (readl(parity_debug_base + parity_record->check_offset) &
			parity_record->check_mask) {
			udelay(1);
		}
	}

	spin_unlock(&parity_isr_lock);

	return IRQ_HANDLED;
}

void __attribute__((weak)) cache_parity_init_platform(void)
{
	pr_info("[%s] adopt default flow\n", __func__);
}

static void cache_parity_clear_irq_flags(void *arg)
{
	struct cache_parity_irq_data *irq_data = arg;

	irq_clear_status_flags(irq_data->irq, irq_data->status_flags);
}

static int cache_parity_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct cache_parity_v2_data *data =
		container_of(node, struct cache_parity_v2_data, cpuhp_node);
	struct cache_parity_irq_data *irq_data;
	int ret;

	if (cpu >= data->core_count)
		return 0;

	irq_data = &data->irqs[cpu];
	ret = irq_force_affinity(irq_data->irq, cpumask_of(cpu));
	if (ret)
		return ret;

	if (atomic_cmpxchg(&irq_data->hotplug_disabled, 1, 0) == 1)
		enable_irq(irq_data->irq);

	return 0;
}

static int cache_parity_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct cache_parity_v2_data *data =
		container_of(node, struct cache_parity_v2_data, cpuhp_node);
	struct cache_parity_irq_data *irq_data;
	unsigned long flags;

	if (cpu >= data->core_count)
		return 0;

	irq_data = &data->irqs[cpu];
	if (atomic_cmpxchg(&irq_data->hotplug_disabled, 0, 1) == 0) {
		disable_irq(irq_data->irq);
		local_irq_save(flags);
		if (WARN_ON_ONCE(raw_smp_processor_id() != cpu))
			goto restore_irqs;

		/* Keep the PE-local selector stable against the DSU fault IRQ. */
		write_ERXSELR_EL1(0);
		isb();
		if (read_ERXSTATUS_EL1() & ERXSTATUS_V_BIT) {
			if (custom_parity_isr)
				custom_parity_isr(irq_data->irq, irq_data);
			else
				default_parity_isr_v2(irq_data->irq, irq_data);
		}

restore_irqs:
		local_irq_restore(flags);
	}

	return 0;
}

static int cache_parity_probe_v2(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct cache_parity_v2_data *data;
	unsigned int i;
	int count;
	int ret;
	int irq;

	cache_parity_init_platform();

	ret = of_property_read_u32(node, "err_level", &err_level);
	if (ret)
		return ret;

	count = of_irq_count(node);
	if (count <= 0)
		return count ? count : -EINVAL;
	irq_count = count;
	if (irq_count - 1 > nr_cpu_ids) {
		dev_err(&pdev->dev, "%u core IRQs exceed nr_cpu_ids=%u\n",
			irq_count - 1, nr_cpu_ids);
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->core_count = irq_count - 1;
	data->irqs = devm_kcalloc(&pdev->dev, irq_count,
				  sizeof(*data->irqs), GFP_KERNEL);
	if (!data->irqs)
		return -ENOMEM;

	for (i = 0; i < irq_count; i++) {
		struct cache_parity_irq_data *irq_data = &data->irqs[i];

		irq = irq_of_parse_and_map(node, i);
		if (!irq) {
			dev_err(&pdev->dev, "failed to map irq index %u\n", i);
			return -EINVAL;
		}
		pr_debug("irq %d for cpu%d\n", irq, i);
		irq_data->irq = irq;
		irq_data->hwirq = virq_to_hwirq(irq);
		if ((i < data->core_count &&
		     irq_data->hwirq != FAULTIRQ_START + i + 1) ||
		    (i == data->core_count &&
		     irq_data->hwirq != FAULTIRQ_START)) {
			dev_err(&pdev->dev,
				"invalid fault IRQ topology at index %u: hwirq%u\n",
				i, irq_data->hwirq);
			return -EINVAL;
		}
		atomic_set(&irq_data->error_count, 0);
		atomic_set(&irq_data->spurious_count, 0);
		atomic_set(&irq_data->irq_disabled, 0);
		atomic_set(&irq_data->hotplug_disabled, 0);

		/*
		 * we only need to bind nFAULTIRQ[n:1] to the
		 * corressponding cpu, and the end of list is
		 * dsu, which is able to serve by any core.
		 */
		irq_data->status_flags = IRQ_DISABLE_UNLAZY;
		if (i < irq_count - 1) {
			irq_data->status_flags |=
				IRQ_NOAUTOEN | IRQ_NO_BALANCING;
			atomic_set(&irq_data->hotplug_disabled, 1);
		}
		irq_set_status_flags(irq, irq_data->status_flags);
		ret = devm_add_action_or_reset(&pdev->dev,
					       cache_parity_clear_irq_flags,
					       irq_data);
		if (ret)
			return ret;

		if (custom_parity_isr)
			ret = devm_request_irq(&pdev->dev, irq,
					       custom_parity_isr,
					       IRQF_TRIGGER_NONE | IRQF_ONESHOT,
					       "cache_parity", irq_data);
		else
			ret = devm_request_irq(&pdev->dev, irq,
					       default_parity_isr_v2,
					       IRQF_TRIGGER_NONE | IRQF_ONESHOT,
					       "cache_parity", irq_data);
		if (ret) {
			dev_err(&pdev->dev, "request_irq(%u) failed: %d\n", i, ret);
			return ret;
		}
	}

	ret = cpuhp_state_add_instance(cache_parity_cpuhp_state,
				       &data->cpuhp_node);
	if (ret) {
		dev_err(&pdev->dev, "failed to register CPU hotplug: %d\n", ret);
		return ret;
	}
	platform_set_drvdata(pdev, data);

	return 0;
}

static int cache_parity_probe_v1(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct parity_irq_config_t *parity_irq_config;
	size_t size;
	unsigned int i;
	unsigned int target_cpu;
	int ret;
	int irq;

	cache_parity_init_platform();

	ret = of_property_read_u32(node, "err_level", &err_level);
	if (ret)
		return ret;

	irq_count = of_irq_count(node);
	pr_info("irq_count: %d, err_level: %d\n", irq_count, err_level);

	size = sizeof(struct parity_irq_record_t) * irq_count;
	parity_irq_record = kmalloc(size, GFP_KERNEL);
	if (!parity_irq_record)
		return -ENOMEM;

	size = sizeof(struct parity_irq_config_t) * irq_count;
	parity_irq_config = kmalloc(size, GFP_KERNEL);
	if (!parity_irq_config)
		return -ENOMEM;

	size = size >> 2;
	of_property_read_variable_u32_array(node, "irq_config",
		(u32 *)parity_irq_config, size, size);

	for (i = 0; i < irq_count; i++) {
		memcpy(
			&(parity_irq_record[i].parity_record),
			&(parity_irq_config[i].parity_record),
			sizeof(struct parity_record_t));

		irq = irq_of_parse_and_map(node, i);
		parity_irq_record[i].irq = irq;
		pr_info("get %d for %d\n", irq, i);

		target_cpu = parity_irq_config[i].target_cpu;
		if (target_cpu != 1024) {
			ret = irq_set_affinity(irq, cpumask_of(target_cpu));
			if (ret)
				pr_info("target_cpu(%d) fail\n", i);
		}

		if (custom_parity_isr)
			ret = request_irq(irq, custom_parity_isr,
				IRQF_TRIGGER_NONE, "cache_parity",
				&cache_parity_drv);
		else
			ret = request_irq(irq, default_parity_isr_v1,
				IRQF_TRIGGER_NONE, "cache_parity",
				&cache_parity_drv);
		if (ret != 0)
			pr_info("request_irq(%d) fail\n", i);
	}

	kfree(parity_irq_config);

	return 0;
}

static int cache_parity_probe(struct platform_device *pdev)
{
	int ret;

	ret = of_property_read_u32(pdev->dev.of_node, "version", &version);
	if (ret)
		return ret;

	switch (version) {
	case 1:
		parity_debug_base = of_iomap(pdev->dev.of_node, 0);
		if (!parity_debug_base)
			return -ENOMEM;

		return cache_parity_probe_v1(pdev);
	case 2:
		return cache_parity_probe_v2(pdev);
	default:
		pr_info("unsupported version\n");
		return 0;
	}
}

static int cache_parity_remove(struct platform_device *pdev)
{
	struct cache_parity_v2_data *data = platform_get_drvdata(pdev);
	unsigned int i;
	int ret;

	if (!data)
		return 0;

	ret = cpuhp_state_remove_instance(cache_parity_cpuhp_state,
					  &data->cpuhp_node);
	for (i = 0; i <= data->core_count; i++)
		disable_irq(data->irqs[i].irq);
	cancel_work_sync(&cache_parity_v2_queue.work);
	cache_parity_reset_queue_v2();
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int __init cache_parity_init(void)
{
	int ret;

	INIT_WORK(&cache_parity_wd.work, handle_error);
	INIT_WORK(&cache_parity_v2_queue.work, handle_error_v2);

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "cache-parity:online",
				      cache_parity_cpu_online,
				      cache_parity_cpu_offline);
	if (ret < 0)
		return ret;
	cache_parity_cpuhp_state = ret;

	ret = platform_driver_register(&cache_parity_drv);
	if (ret) {
		cpuhp_remove_multi_state(cache_parity_cpuhp_state);
		cache_parity_cpuhp_state = -1;
		return ret;
	}

	ret = driver_create_file(&cache_parity_drv.driver,
				 &driver_attr_cache_status);
	if (ret) {
		platform_driver_unregister(&cache_parity_drv);
		cpuhp_remove_multi_state(cache_parity_cpuhp_state);
		cache_parity_cpuhp_state = -1;
		return ret;
	}

	return 0;
}

module_init(cache_parity_init);
