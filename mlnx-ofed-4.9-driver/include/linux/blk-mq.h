#ifndef _COMPAT_LINUX_BLK_MQ_H
#define _COMPAT_LINUX_BLK_MQ_H

#include "../../compat/config.h"
#include <linux/version.h>

#include_next <linux/blk-mq.h>
#ifndef HAVE_BLK_MQ_TAGSET_WAIT_COMPLETED_REQUEST
#include <linux/delay.h>
#endif

#ifndef HAVE_BLK_MQ_MAP_QUEUES
int blk_mq_map_queues(struct blk_mq_tag_set *set);
#endif

#ifndef HAVE_BLK_MQ_FREEZE_QUEUE_WAIT_TIMEOUT
static inline int blk_mq_freeze_queue_wait_timeout(struct request_queue *q,
						   unsigned long timeout)
{
	return wait_event_timeout(q->mq_freeze_wq,
#ifdef HAVE_REQUEST_QUEUE_Q_USAGE_COUNTER
				  percpu_ref_is_zero(&q->q_usage_counter),
#else
				  percpu_ref_is_zero(&q->mq_usage_counter),
#endif
				  timeout);
}
#endif

#ifndef HAVE_BLK_MQ_FREEZE_QUEUE_WAIT
static inline void blk_mq_freeze_queue_wait(struct request_queue *q)
{
#ifdef HAVE_REQUEST_QUEUE_Q_USAGE_COUNTER
	wait_event(q->mq_freeze_wq, percpu_ref_is_zero(&q->q_usage_counter));
#else
	wait_event(q->mq_freeze_wq, percpu_ref_is_zero(&q->mq_usage_counter));
#endif
}
#endif

#if !defined(HAVE_BLK_MQ_TAGSET_BUSY_ITER) && \
	defined(HAVE_BLK_MQ_ALL_TAG_BUSY_ITER)
static inline void blk_mq_tagset_busy_iter(struct blk_mq_tag_set *tagset,
		busy_tag_iter_fn *fn, void *priv)
{
	int i;

	for (i = 0; i < tagset->nr_hw_queues; i++) {
		if (tagset->tags && tagset->tags[i])
			blk_mq_all_tag_busy_iter(tagset->tags[i], fn, priv);
	}
}
#endif

#if !defined(HAVE_REQUEST_TO_QC_T) && defined(HAVE_BLK_TYPES_REQ_HIPRI)
static inline blk_qc_t request_to_qc_t(struct blk_mq_hw_ctx *hctx,
		struct request *rq)
{
	if (rq->tag != -1)
		return rq->tag | (hctx->queue_num << BLK_QC_T_SHIFT);

	return rq->internal_tag | (hctx->queue_num << BLK_QC_T_SHIFT) |
			BLK_QC_T_INTERNAL;
}
#endif

#ifndef HAVE_BLK_STATUS_T

typedef int blk_status_t;
#define BLK_STS_OK		BLK_MQ_RQ_QUEUE_OK
#define BLK_STS_RESOURCE	BLK_MQ_RQ_QUEUE_BUSY
#define BLK_STS_IOERR		BLK_MQ_RQ_QUEUE_ERROR

#define BLK_STS_NOSPC		-ENOSPC
#define BLK_STS_NOTSUPP		-EOPNOTSUPP
#define BLK_STS_MEDIUM		-ENODATA
#define BLK_STS_TIMEOUT		-ETIMEDOUT
#define BLK_STS_TRANSPORT	-ENOLINK
#define BLK_STS_TARGET		-EREMOTEIO
#define BLK_STS_NEXUS		-EBADE
#define BLK_STS_PROTECTION	-EILSEQ

#endif /* HAVE_BLK_STATUS_T */

#ifndef HAVE_BLK_PATH_ERROR
static inline bool blk_path_error(blk_status_t error)
{
	switch (error) {
	case BLK_STS_NOTSUPP:
	case BLK_STS_NOSPC:
	case BLK_STS_TARGET:
	case BLK_STS_NEXUS:
	case BLK_STS_MEDIUM:
	case BLK_STS_PROTECTION:
		return false;
	}

	/* Anything else could be a path failure, so should be retried */
	return true;
}
#endif

#if !defined(HAVE_BLK_MQ_REQUEST_COMPLETED) && \
	defined(HAVE_MQ_RQ_STATE)
static inline enum mq_rq_state blk_mq_rq_state(struct request *rq)
{
	return READ_ONCE(rq->state);
}

static inline int blk_mq_request_completed(struct request *rq)
{
	return blk_mq_rq_state(rq) == MQ_RQ_COMPLETE;
}
#endif

#ifndef HAVE_BLK_MQ_TAGSET_WAIT_COMPLETED_REQUEST
#ifdef HAVE_MQ_RQ_STATE
#ifdef HAVE_BLK_MQ_BUSY_TAG_ITER_FN_BOOL
static inline bool blk_mq_tagset_count_completed_rqs(struct request *rq,
                        void *data, bool reserved)
#else
static inline void blk_mq_tagset_count_completed_rqs(struct request *rq,
                        void *data, bool reserved)
#endif
{
   unsigned *count = data;

   if (blk_mq_request_completed(rq))
       (*count)++;
#ifdef HAVE_BLK_MQ_BUSY_TAG_ITER_FN_BOOL
   return true;
#endif
}

static inline void
blk_mq_tagset_wait_completed_request(struct blk_mq_tag_set *tagset)
{
   while (true) {
       unsigned count = 0;

       blk_mq_tagset_busy_iter(tagset,
               blk_mq_tagset_count_completed_rqs, &count);
       if (!count)
           break;
       msleep(5);
   }
}
#else
static inline void
blk_mq_tagset_wait_completed_request(struct blk_mq_tag_set *tagset)
{
	msleep(100);
}
#endif /* HAVE_MQ_RQ_STATE */
#endif /* HAVE_BLK_MQ_TAGSET_WAIT_COMPLETED_REQUEST */

#endif /* _COMPAT_LINUX_BLK_MQ_H */
