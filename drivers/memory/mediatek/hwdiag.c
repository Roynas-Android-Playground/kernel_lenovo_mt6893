// SPDX-License-Identifier: GPL-2.0
/*
 * Opt-in MediaTek boot cache/DRAM diagnostic.
 *
 * This is deliberately command-line gated because a failing platform may
 * take a synchronous external abort while the test touches memory.
 */

#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <asm/barrier.h>
#include <asm/cacheflush.h>

#define HWDIAG_CACHE_BYTES	SZ_256K
#define HWDIAG_CACHE_PASSES	128
#define HWDIAG_DRAM_BYTES	SZ_8M
#define HWDIAG_DRAM_PASSES	4
#define HWDIAG_PARALLEL_PASSES	256

#define ERXSTATUS_UE_BIT		(1ULL << 29)
#define ERXSTATUS_CE_MASK	(3ULL << 24)
#define ERXSTATUS_DE_BIT		(1ULL << 23)

enum hwdiag_mode {
	HWDIAG_DISABLED,
	HWDIAG_CACHE,
	HWDIAG_FULL,
};

struct hwdiag_test {
	u64 *buffer;
	size_t bytes;
	unsigned int passes;
	bool flush_to_poc;
	const char *name;
};

struct hwdiag_worker {
	struct task_struct *task;
	struct completion ready;
	struct completion start;
	struct completion done;
	struct hwdiag_test test;
	int cpu;
	int result;
};

struct hwdiag_ras_record {
	u64 status;
	u64 misc0;
};

static enum hwdiag_mode hwdiag_mode __initdata;

static int __init hwdiag_setup(char *arg)
{
	if (!arg || !strcmp(arg, "cache") || !strcmp(arg, "1"))
		hwdiag_mode = HWDIAG_CACHE;
	else if (!strcmp(arg, "full") || !strcmp(arg, "2"))
		hwdiag_mode = HWDIAG_FULL;
	else if (!strcmp(arg, "off") || !strcmp(arg, "0"))
		hwdiag_mode = HWDIAG_DISABLED;
	else
		return -EINVAL;

	return 0;
}
early_param("mtk_hwdiag", hwdiag_setup);

static inline u64 hwdiag_read_erxselr(void)
{
	u64 value;

	asm volatile("mrs %0, s3_0_c5_c3_1" : "=r" (value));
	return value;
}

static inline void hwdiag_write_erxselr(u64 value)
{
	asm volatile("msr s3_0_c5_c3_1, %0" : : "r" (value));
	isb();
}

static inline u64 hwdiag_read_erxstatus(void)
{
	u64 value;

	asm volatile("mrs %0, s3_0_c5_c4_2" : "=r" (value));
	return value;
}

static inline u64 hwdiag_read_erxmisc0(void)
{
	u64 value;

	asm volatile("mrs %0, s3_0_c5_c5_0" : "=r" (value));
	return value;
}

static void hwdiag_log_ras(const char *phase)
{
	struct hwdiag_ras_record record[2];
	unsigned long flags;
	u64 old_selector;
	int cpu = raw_smp_processor_id();
	unsigned int selector;

	local_irq_save(flags);
	old_selector = hwdiag_read_erxselr();
	for (selector = 0; selector < ARRAY_SIZE(record); selector++) {
		hwdiag_write_erxselr(selector);
		record[selector].status = hwdiag_read_erxstatus();
		record[selector].misc0 = hwdiag_read_erxmisc0();
	}
	hwdiag_write_erxselr(old_selector);
	local_irq_restore(flags);

	for (selector = 0; selector < ARRAY_SIZE(record); selector++) {
		u64 status = record[selector].status;

		pr_notice("MTK-HWDIAG: cpu%d %s record%u status=%016llx misc0=%016llx CE=%llu UE=%llu DE=%llu\n",
			  cpu, phase, selector, status,
			  record[selector].misc0,
			  (status & ERXSTATUS_CE_MASK) >> 24,
			  !!(status & ERXSTATUS_UE_BIT),
			  !!(status & ERXSTATUS_DE_BIT));
	}
}

static u64 hwdiag_expected(unsigned int pass, size_t index)
{
	static const u64 patterns[] = {
		0x0000000000000000ULL,
		0xffffffffffffffffULL,
		0x5555555555555555ULL,
		0xaaaaaaaaaaaaaaaaULL,
	};

	return patterns[pass % ARRAY_SIZE(patterns)] ^
		((u64)index * 0x9e3779b97f4a7c15ULL);
}

static int hwdiag_test_buffer(struct hwdiag_test *test)
{
	size_t words = test->bytes / sizeof(*test->buffer);
	unsigned int pass;
	int cpu = raw_smp_processor_id();

	pr_notice("MTK-HWDIAG: cpu%d start %s bytes=%zu passes=%u flush=%u\n",
		  cpu, test->name, test->bytes, test->passes,
		  test->flush_to_poc);
	hwdiag_log_ras("before");

	for (pass = 0; pass < test->passes; pass++) {
		size_t index;

		for (index = 0; index < words; index++)
			WRITE_ONCE(test->buffer[index],
				   hwdiag_expected(pass, index));

		/* Complete pattern writes before flushing or verifying them. */
		mb();
		if (test->flush_to_poc) {
			__flush_dcache_area((void *)test->buffer, test->bytes);
			dsb(sy);
		}

		for (index = 0; index < words; index++) {
			u64 expected = hwdiag_expected(pass, index);
			u64 actual = READ_ONCE(test->buffer[index]);

			if (actual != expected) {
				pr_emerg("MTK-HWDIAG: FAIL cpu%d %s pass=%u offset=%zx expected=%016llx actual=%016llx\n",
					 cpu, test->name, pass,
					 index * sizeof(*test->buffer),
					 expected, actual);
				hwdiag_log_ras("failure");
				return -EIO;
			}
		}

		cond_resched();
	}

	hwdiag_log_ras("after");
	pr_notice("MTK-HWDIAG: cpu%d pass %s\n", cpu, test->name);
	return 0;
}

static long hwdiag_work_on_cpu(void *arg)
{
	return hwdiag_test_buffer(arg);
}

static int hwdiag_run_sequential(struct hwdiag_test *test)
{
	int failures = 0;
	int cpu;

	for_each_online_cpu(cpu) {
		long ret = work_on_cpu_safe(cpu, hwdiag_work_on_cpu, test);

		if (ret) {
			failures++;
			pr_emerg("MTK-HWDIAG: cpu%d %s returned %ld\n",
				 cpu, test->name, ret);
		}
	}

	return failures;
}

static int hwdiag_parallel_worker(void *arg)
{
	struct hwdiag_worker *worker = arg;

	complete(&worker->ready);
	wait_for_completion(&worker->start);
	worker->result = hwdiag_test_buffer(&worker->test);
	complete(&worker->done);
	return 0;
}

static int hwdiag_run_parallel(void)
{
	struct hwdiag_worker *workers;
	int failures = 0;
	int cpu;

	workers = kcalloc(nr_cpu_ids, sizeof(*workers), GFP_KERNEL);
	if (!workers)
		return -ENOMEM;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		struct hwdiag_worker *worker = &workers[cpu];

		worker->cpu = cpu;
		worker->test.buffer = vzalloc(HWDIAG_CACHE_BYTES);
		worker->test.bytes = HWDIAG_CACHE_BYTES;
		worker->test.passes = HWDIAG_PARALLEL_PASSES;
		worker->test.flush_to_poc = false;
		worker->test.name = "parallel-cache";
		init_completion(&worker->ready);
		init_completion(&worker->start);
		init_completion(&worker->done);

		if (!worker->test.buffer) {
			failures++;
			continue;
		}

		worker->task = kthread_create(hwdiag_parallel_worker, worker,
					      "mtk_hwdiag/%d", cpu);
		if (IS_ERR(worker->task)) {
			pr_emerg("MTK-HWDIAG: cannot create cpu%d worker: %ld\n",
				 cpu, PTR_ERR(worker->task));
			worker->task = NULL;
			failures++;
			continue;
		}

		kthread_bind(worker->task, cpu);
		wake_up_process(worker->task);
	}

	for_each_online_cpu(cpu) {
		if (workers[cpu].task)
			wait_for_completion(&workers[cpu].ready);
	}
	pr_notice("MTK-HWDIAG: starting parallel cache/power phase\n");
	for_each_online_cpu(cpu) {
		if (workers[cpu].task)
			complete(&workers[cpu].start);
	}
	for_each_online_cpu(cpu) {
		if (!workers[cpu].task)
			continue;
		wait_for_completion(&workers[cpu].done);
		if (workers[cpu].result)
			failures++;
	}
	put_online_cpus();

	for (cpu = 0; cpu < nr_cpu_ids; cpu++)
		vfree((void *)workers[cpu].test.buffer);
	kfree(workers);
	return failures;
}

static int __init mtk_hwdiag_init(void)
{
	struct hwdiag_test cache_test;
	struct hwdiag_test dram_test;
	int failures = 0;
	int ret;

	if (hwdiag_mode == HWDIAG_DISABLED)
		return 0;

	pr_notice("MTK-HWDIAG: begin mode=%s online_cpus=%u\n",
		  hwdiag_mode == HWDIAG_FULL ? "full" : "cache",
		  num_online_cpus());

	cache_test.buffer = vzalloc(HWDIAG_CACHE_BYTES);
	if (!cache_test.buffer)
		return -ENOMEM;
	cache_test.bytes = HWDIAG_CACHE_BYTES;
	cache_test.passes = HWDIAG_CACHE_PASSES;
	cache_test.flush_to_poc = false;
	cache_test.name = "sequential-cache";
	failures += hwdiag_run_sequential(&cache_test);
	vfree((void *)cache_test.buffer);

	if (hwdiag_mode == HWDIAG_FULL) {
		dram_test.buffer = vzalloc(HWDIAG_DRAM_BYTES);
		if (!dram_test.buffer) {
			failures++;
		} else {
			dram_test.bytes = HWDIAG_DRAM_BYTES;
			dram_test.passes = HWDIAG_DRAM_PASSES;
			dram_test.flush_to_poc = true;
			dram_test.name = "sequential-dram";
			failures += hwdiag_run_sequential(&dram_test);
			vfree((void *)dram_test.buffer);
		}
		ret = hwdiag_run_parallel();
		if (ret < 0)
			failures++;
		else
			failures += ret;
	}

	if (failures)
		pr_emerg("MTK-HWDIAG: completed with %d failure(s)\n",
			 failures);
	else
		pr_notice("MTK-HWDIAG: PASS\n");

	return 0;
}
device_initcall_sync(mtk_hwdiag_init);
