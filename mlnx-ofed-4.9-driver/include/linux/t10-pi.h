#ifndef _COMPAT_LINUX_T10_PI_H
#define _COMPAT_LINUX_T10_PI_H 1

#include "../../compat/config.h"

#ifdef HAVE_T10_PI_H

#include_next <linux/t10-pi.h>

#else

#include <linux/types.h>
#include <linux/blkdev.h>

/*
 * T10 Protection Information tuple.
 */
struct t10_pi_tuple {
	__be16 guard_tag;	/* Checksum */
	__be16 app_tag;		/* Opaque storage */
	__be32 ref_tag;		/* Target LBA or indirect LBA */
};

#define T10_PI_APP_ESCAPE cpu_to_be16(0xffff)
#define T10_PI_REF_ESCAPE cpu_to_be32(0xffffffff)

extern const struct blk_integrity_profile t10_pi_type1_crc;
extern const struct blk_integrity_profile t10_pi_type1_ip;
extern const struct blk_integrity_profile t10_pi_type3_crc;
extern const struct blk_integrity_profile t10_pi_type3_ip;
#endif

/*
 * A T10 PI-capable target device can be formatted with different
 * protection schemes.	Currently 0 through 3 are defined:
 *
 * Type 0 is regular (unprotected) I/O
 *
 * Type 1 defines the contents of the guard and reference tags
 *
 * Type 2 defines the contents of the guard and reference tags and
 * uses 32-byte commands to seed the latter
 *
 * Type 3 defines the contents of the guard tag only
 */
#ifndef HAVE_T10_DIF_TYPE
enum t10_dif_type {
	T10_PI_TYPE0_PROTECTION = 0x0,
	T10_PI_TYPE1_PROTECTION = 0x1,
	T10_PI_TYPE2_PROTECTION = 0x2,
	T10_PI_TYPE3_PROTECTION = 0x3,
};
#endif

#ifndef HAVE_T10_PI_REF_TAG
static inline u32 t10_pi_ref_tag(struct request *rq)
{
	unsigned int shift = ilog2(queue_logical_block_size(rq->q));

#ifdef CONFIG_BLK_DEV_INTEGRITY
#ifdef HAVE_REQUEST_QUEUE_INTEGRITY
	if (rq->q->integrity.interval_exp)
		shift = rq->q->integrity.interval_exp;
#else
#ifdef HAVE_BLK_INTEGRITY_SECTOR_SIZE
	if (blk_get_integrity(rq->rq_disk) &&
	    blk_get_integrity(rq->rq_disk)->sector_size)
		shift = blk_get_integrity(rq->rq_disk)->sector_size;
#else
	if (blk_get_integrity(rq->rq_disk) &&
	    blk_get_integrity(rq->rq_disk)->interval)
		shift = blk_get_integrity(rq->rq_disk)->interval;
#endif
#endif /* CONFIG_BLK_DEV_INTEGRITY */
#endif /* HAVE_REQUEST_QUEUE_INTEGRITY */
	return blk_rq_pos(rq) >> (shift - SECTOR_SHIFT) & 0xffffffff;
}
#endif /* HAVE_T10_PI_REF_TAG */

#ifndef HAVE_T10_PI_PREPARE
#ifdef CONFIG_BLK_DEV_INTEGRITY
static inline void t10_pi_prepare(struct request *rq, u8 protection_type)
{
	const int tuple_sz = sizeof(struct t10_pi_tuple);
	u32 ref_tag = t10_pi_ref_tag(rq);
	struct bio *bio;

	if (protection_type == T10_PI_TYPE3_PROTECTION)
		return;

	__rq_for_each_bio(bio, rq) {
#ifdef HAVE_BIO_BIP_GET_SEED
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u32 virt = bip_get_seed(bip) & 0xffffffff;
		struct bio_vec iv;
		struct bvec_iter iter;

		/* Already remapped? */
		if (bip->bip_flags & BIP_MAPPED_INTEGRITY)
			break;

		bip_for_each_vec(iv, bip, iter) {
			void *p, *pmap;
			unsigned int j;

			pmap = kmap_atomic(iv.bv_page);
			p = pmap + iv.bv_offset;
			for (j = 0; j < iv.bv_len; j += tuple_sz) {
				struct t10_pi_tuple *pi = p;

				if (be32_to_cpu(pi->ref_tag) == virt)
					pi->ref_tag = cpu_to_be32(ref_tag);
				virt++;
				ref_tag++;
				p += tuple_sz;
			}

			kunmap_atomic(pmap);
		}

		bip->bip_flags |= BIP_MAPPED_INTEGRITY;
#else
		u32 virt;
		struct bio_vec *iv;
		unsigned int i;

		/* Already remapped? */
		if (bio_flagged(bio, BIO_MAPPED_INTEGRITY))
			break;

		virt = bio->bi_integrity->bip_sector & 0xffffffff;

		bip_for_each_vec(iv, bio->bi_integrity, i) {
			void *p, *pmap;
			unsigned int j;

			pmap = kmap_atomic(iv->bv_page);
			p = pmap + iv->bv_offset;
			for (j = 0; j < iv->bv_len; j += tuple_sz) {
				struct t10_pi_tuple *pi = p;

				if (be32_to_cpu(pi->ref_tag) == virt)
					pi->ref_tag = cpu_to_be32(ref_tag);
				virt++;
				ref_tag++;
				p += tuple_sz;
			}

			kunmap_atomic(pmap);
		}

		bio->bi_flags |= (1 << BIO_MAPPED_INTEGRITY);
#endif
	}
}

static inline void t10_pi_complete(struct request *rq, u8 protection_type,
				   unsigned int intervals)
{
	const int tuple_sz = sizeof(struct t10_pi_tuple);
	u32 ref_tag = t10_pi_ref_tag(rq);
	struct bio *bio;

	if (protection_type == T10_PI_TYPE3_PROTECTION)
		return;

	__rq_for_each_bio(bio, rq) {
#ifdef HAVE_BIO_BIP_GET_SEED
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u32 virt = bip_get_seed(bip) & 0xffffffff;
		struct bio_vec iv;
		struct bvec_iter iter;

		bip_for_each_vec(iv, bip, iter) {
			void *p, *pmap;
			unsigned int j;

			pmap = kmap_atomic(iv.bv_page);
			p = pmap + iv.bv_offset;
			for (j = 0; j < iv.bv_len && intervals; j += tuple_sz) {
				struct t10_pi_tuple *pi = p;

				if (be32_to_cpu(pi->ref_tag) == ref_tag)
					pi->ref_tag = cpu_to_be32(virt);
				virt++;
				ref_tag++;
				intervals--;
				p += tuple_sz;
			}

			kunmap_atomic(pmap);
		}
#else
		u32 virt = bio->bi_integrity->bip_sector & 0xffffffff;
		struct bio_vec *iv;
		unsigned int i;

		bip_for_each_vec(iv, bio->bi_integrity, i) {
			void *p, *pmap;
			unsigned int j;

			pmap = kmap_atomic(iv->bv_page);
			p = pmap + iv->bv_offset;
			for (j = 0; j < iv->bv_len && intervals; j += tuple_sz) {
				struct t10_pi_tuple *pi = p;

				if (be32_to_cpu(pi->ref_tag) == ref_tag)
					pi->ref_tag = cpu_to_be32(virt);
				virt++;
				ref_tag++;
				intervals--;
				p += tuple_sz;
			}

			kunmap_atomic(pmap);
		}
#endif
	}
}
#else
static inline void t10_pi_complete(struct request *rq, u8 protection_type,
				   unsigned int intervals)
{
}
static inline void t10_pi_prepare(struct request *rq, u8 protection_type)
{
}
#endif
#endif /* HAVE_T10_PI_PREPARE */

#endif /* _COMPAT_LINUX_T10_PI_H */
