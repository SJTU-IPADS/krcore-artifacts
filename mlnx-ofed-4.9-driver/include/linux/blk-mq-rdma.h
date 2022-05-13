#ifndef _LINUX_BLK_MQ_RDMA_H
#define _LINUX_BLK_MQ_RDMA_H

#include "../../compat/config.h"

#ifdef HAVE_BLK_MQ_TAG_SET_HAS_MAP
struct blk_mq_tag_set;
struct ib_device;

#define blk_mq_rdma_map_queues LINUX_BACKPORT(blk_mq_rdma_map_queues)
#ifdef HAVE_BLK_MQ_RDMA_MAP_QUEUES_MAP
int blk_mq_rdma_map_queues(struct blk_mq_queue_map *map,
		struct ib_device *dev, int first_vec);
#else
int blk_mq_rdma_map_queues(struct blk_mq_tag_set *set,
		struct ib_device *dev, int first_vec);
#endif
#endif /* HAVE_BLK_MQ_TAG_SET_HAS_MAP */

#endif /* _LINUX_BLK_MQ_RDMA_H */
