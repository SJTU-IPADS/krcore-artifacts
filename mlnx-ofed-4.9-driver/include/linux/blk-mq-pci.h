#ifndef _COMPAT_LINUX_BLK_MQ_PCI_H
#define _COMPAT_LINUX_BLK_MQ_PCI_H 1

#include "../../compat/config.h"

#ifdef HAVE_BLK_MQ_PCI_H
#include_next <linux/blk-mq-pci.h>
#endif

#if defined(HAVE_BLK_MQ_OPS_MAP_QUEUES) && \
	!defined(HAVE_BLK_MQ_PCI_MAP_QUEUES_3_ARGS) && \
	defined(HAVE_PCI_IRQ_GET_AFFINITY)

#include <linux/blk-mq.h>
#include <linux/pci.h>

static inline
int __blk_mq_pci_map_queues(struct blk_mq_tag_set *set, struct pci_dev *pdev,
			    int offset)
{
	const struct cpumask *mask;
	unsigned int queue, cpu;

	for (queue = 0; queue < set->nr_hw_queues; queue++) {
		mask = pci_irq_get_affinity(pdev, queue + offset);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			set->mq_map[cpu] = queue;
	}

	return 0;

fallback:
	WARN_ON_ONCE(set->nr_hw_queues > 1);
	for_each_possible_cpu(cpu)
		set->mq_map[cpu] = 0;
	return 0;
}
#endif

#endif	/* _COMPAT_LINUX_BLK_MQ_PCI_H */
