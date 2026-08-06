/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <mtk_lpm_module.h>
#include <mtk_lpm_internal.h>
#include <mtk_lpm_trace.h>


struct MTK_LPM_TRACE_INS {
	void __iomem *mmu;
	size_t size;
};

static struct MTK_LPM_TRACE_INS mtk_lpm_trace_ins;

int __init mtk_lpm_trace_parsing(struct device_node *parent)
{
	struct device_node *node;

	if (!parent)
		return -EINVAL;

	node = of_find_compatible_node(parent, NULL,
					"mediatek,lpm-sysram");
	if (node) {
		struct resource res;

		if (!of_address_to_resource(node, 0, &res)) {
			mtk_lpm_trace_ins.size = (size_t)resource_size(&res);
			mtk_lpm_trace_ins.mmu = ioremap(res.start,
							resource_size(&res));
		}
		of_node_put(node);
	}
	return 0;
}

size_t mtk_lpm_trace_sysram_read(unsigned long offset,
					 void *buf, size_t sz)
{
	size_t rSz;

	if (!buf || !mtk_lpm_trace_ins.mmu ||
	    offset >= mtk_lpm_trace_ins.size)
		return 0;

	rSz = min_t(size_t, sz, mtk_lpm_trace_ins.size - offset);

	memcpy_fromio(buf,
		      (u8 __iomem *)mtk_lpm_trace_ins.mmu + offset,
		      rSz);
	return rSz;
}

size_t mtk_lpm_trace_sysram_write(unsigned long offset,
					 const void *buf, size_t sz)
{
	size_t rSz;

	if (!buf || !mtk_lpm_trace_ins.mmu ||
	    offset >= mtk_lpm_trace_ins.size)
		return 0;

	rSz = min_t(size_t, sz, mtk_lpm_trace_ins.size - offset);

	memcpy_toio((u8 __iomem *)mtk_lpm_trace_ins.mmu + offset,
		    buf, rSz);
	return rSz;
}

int mtk_lpm_trace_instance_get(int type, struct MTK_LPM_PLAT_TRACE *ins)
{
	int ret = 0;

	if (!ins)
		return -EINVAL;

	if (type == MT_LPM_PLAT_TRACE_SYSRAM) {
		ins->read = mtk_lpm_trace_sysram_read;
		ins->write = mtk_lpm_trace_sysram_write;
	} else
		ret = -EINVAL;

	return ret;
}
