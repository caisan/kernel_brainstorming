/*
 * Functions related to segment and merge handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>

#include "blk.h"
/*****hujunpeng test***********************************************************/
extern int block_open_printk;


static unsigned int __blk_recalc_rq_segments(struct request_queue *q,
					     struct bio *bio,
					     bool no_sg_merge)
{
	struct bio_vec *bv, *bvprv = NULL;
	int cluster, i, high, highprv = 1;
	unsigned int seg_size, nr_phys_segs;
	struct bio *fbio, *bbio;

	if (!bio)
		return 0;

	fbio = bio;
	cluster = blk_queue_cluster(q);
	seg_size = 0;
	nr_phys_segs = 0;
	high = 0;
	for_each_bio(bio) {
		bio_for_each_segment(bv, bio, i) {
			/*
			 * If SG merging is disabled, each bio vector is
			 * a segment
			 */
			if (no_sg_merge)
				goto new_segment;

			/*
			 * the trick here is making sure that a high page is
			 * never considered part of another segment, since
			 * that might change with the bounce page.
			 */
			high = page_to_pfn(bv->bv_page) > queue_bounce_pfn(q);
			if (high || highprv)
				goto new_segment;
			if (cluster) {
				if (seg_size + bv->bv_len
				    > queue_max_segment_size(q))
					goto new_segment;
				if (!BIOVEC_PHYS_MERGEABLE(bvprv, bv))
					goto new_segment;
				if (!BIOVEC_SEG_BOUNDARY(q, bvprv, bv))
					goto new_segment;

				seg_size += bv->bv_len;
				bvprv = bv;
				continue;
			}
new_segment:
			if (nr_phys_segs == 1 && seg_size >
			    fbio->bi_seg_front_size)
				fbio->bi_seg_front_size = seg_size;

			nr_phys_segs++;
			bvprv = bv;
			seg_size = bv->bv_len;
			highprv = high;
		}
		bbio = bio;
	}

	if (nr_phys_segs == 1 && seg_size > fbio->bi_seg_front_size)
		fbio->bi_seg_front_size = seg_size;
	if (seg_size > bbio->bi_seg_back_size)
		bbio->bi_seg_back_size = seg_size;

	return nr_phys_segs;
}

void blk_recalc_rq_segments(struct request *rq)
{
	bool no_sg_merge = !!test_bit(QUEUE_FLAG_NO_SG_MERGE,
			&rq->q->queue_flags);

	rq->nr_phys_segments = __blk_recalc_rq_segments(rq->q, rq->bio,
			no_sg_merge);
}

void blk_recount_segments(struct request_queue *q, struct bio *bio)
{
	if (test_bit(QUEUE_FLAG_NO_SG_MERGE, &q->queue_flags) &&
			bio->bi_vcnt < queue_max_segments(q))
		bio->bi_phys_segments = bio->bi_vcnt;
	else {
		struct bio *nxt = bio->bi_next;

		bio->bi_next = NULL;
		bio->bi_phys_segments = __blk_recalc_rq_segments(q, bio, false);
		bio->bi_next = nxt;
	}

	bio->bi_flags |= (1 << BIO_SEG_VALID);
}
EXPORT_SYMBOL(blk_recount_segments);

static int blk_phys_contig_segment(struct request_queue *q, struct bio *bio,
				   struct bio *nxt)
{
	if (!blk_queue_cluster(q))
		return 0;

	if (bio->bi_seg_back_size + nxt->bi_seg_front_size >
	    queue_max_segment_size(q))
		return 0;

	if (!bio_has_data(bio))
		return 1;

	if (!BIOVEC_PHYS_MERGEABLE(__BVEC_END(bio), __BVEC_START(nxt)))
		return 0;

	/*
	 * bio and nxt are contiguous in memory; check if the queue allows
	 * these two to be merged into one
	 */
	if (BIO_SEG_BOUNDARY(q, bio, nxt))
		return 1;

	return 0;
}

static void
__blk_segment_map_sg(struct request_queue *q, struct bio_vec *bvec,
		     struct scatterlist *sglist, struct bio_vec **bvprv,
		     struct scatterlist **sg, int *nsegs, int *cluster)
{

	int nbytes = bvec->bv_len;

	if (*bvprv && *cluster) {
		if ((*sg)->length + nbytes > queue_max_segment_size(q))
			goto new_segment;

		if (!BIOVEC_PHYS_MERGEABLE(*bvprv, bvec))
			goto new_segment;
		if (!BIOVEC_SEG_BOUNDARY(q, *bvprv, bvec))
			goto new_segment;

		(*sg)->length += nbytes;
	} else {
new_segment:
		if (!*sg)
			*sg = sglist;
		else {
			/*
			 * If the driver previously mapped a shorter
			 * list, we could see a termination bit
			 * prematurely unless it fully inits the sg
			 * table on each mapping. We KNOW that there
			 * must be more entries here or the driver
			 * would be buggy, so force clear the
			 * termination bit to avoid doing a full
			 * sg_init_table() in drivers for each command.
			 */
			sg_unmark_end(*sg);
			*sg = sg_next(*sg);
		}

		sg_set_page(*sg, bvec->bv_page, nbytes, bvec->bv_offset);
		(*nsegs)++;
	}
	*bvprv = bvec;
}

/*
 * map a request to scatterlist, return number of sg entries setup. Caller
 * must make sure sg can hold rq->nr_phys_segments entries
 */
int blk_rq_map_sg(struct request_queue *q, struct request *rq,
		  struct scatterlist *sglist)
{
	struct bio_vec *bvec, *bvprv;
	struct req_iterator iter;
	struct scatterlist *sg;
	int nsegs, cluster;

	nsegs = 0;
	cluster = blk_queue_cluster(q);

	/*
	 * for each bio in rq
	 */
	bvprv = NULL;
	sg = NULL;
	rq_for_each_segment(bvec, rq, iter) {
		__blk_segment_map_sg(q, bvec, sglist, &bvprv, &sg,
				     &nsegs, &cluster);
	} /* segments in rq */


	if (unlikely(rq->cmd_flags & REQ_COPY_USER) &&
	    (blk_rq_bytes(rq) & q->dma_pad_mask)) {
		unsigned int pad_len =
			(q->dma_pad_mask & ~blk_rq_bytes(rq)) + 1;

		sg->length += pad_len;
		rq->extra_len += pad_len;
	}

	if (q->dma_drain_size && q->dma_drain_needed(rq)) {
		if (rq->cmd_flags & REQ_WRITE)
			memset(q->dma_drain_buffer, 0, q->dma_drain_size);

		sg_unmark_end(sg);
		sg = sg_next(sg);
		sg_set_page(sg, virt_to_page(q->dma_drain_buffer),
			    q->dma_drain_size,
			    ((unsigned long)q->dma_drain_buffer) &
			    (PAGE_SIZE - 1));
		nsegs++;
		rq->extra_len += q->dma_drain_size;
	}

	if (sg)
		sg_mark_end(sg);

	return nsegs;
}
EXPORT_SYMBOL(blk_rq_map_sg);

/**
 * blk_bio_map_sg - map a bio to a scatterlist
 * @q: request_queue in question
 * @bio: bio being mapped
 * @sglist: scatterlist being mapped
 *
 * Note:
 *    Caller must make sure sg can hold bio->bi_phys_segments entries
 *
 * Will return the number of sg entries setup
 */
int blk_bio_map_sg(struct request_queue *q, struct bio *bio,
		   struct scatterlist *sglist)
{
	struct bio_vec *bvec, *bvprv;
	struct scatterlist *sg;
	int nsegs, cluster;
	unsigned long i;

	nsegs = 0;
	cluster = blk_queue_cluster(q);

	bvprv = NULL;
	sg = NULL;
	bio_for_each_segment(bvec, bio, i) {
		__blk_segment_map_sg(q, bvec, sglist, &bvprv, &sg,
				     &nsegs, &cluster);
	} /* segments in bio */

	if (sg)
		sg_mark_end(sg);

	BUG_ON(bio->bi_phys_segments && nsegs > bio->bi_phys_segments);
	return nsegs;
}
EXPORT_SYMBOL(blk_bio_map_sg);

static inline int ll_new_hw_segment(struct request_queue *q,
				    struct request *req,
				    struct bio *bio)
{
	int nr_phys_segs = bio_phys_segments(q, bio);

	if (req->nr_phys_segments + nr_phys_segs > queue_max_segments(q))
		goto no_merge;

	if (bio_integrity(bio) && blk_integrity_merge_bio(q, req, bio))
		goto no_merge;

	/*
	 * This will form the start of a new hw segment.  Bump both
	 * counters.
	 */
	req->nr_phys_segments += nr_phys_segs;
	return 1;

no_merge:
	req_set_nomerge(q, req);
	return 0;
}

int ll_back_merge_fn(struct request_queue *q, struct request *req,
		     struct bio *bio)
{
	if (req_gap_back_merge(req, bio))
		return 0;
	if (blk_rq_sectors(req) + bio_sectors(bio) >
	    blk_rq_get_max_sectors(req)) {
		req_set_nomerge(q, req);
		return 0;
	}
	if (!bio_flagged(req->biotail, BIO_SEG_VALID))
		blk_recount_segments(q, req->biotail);
	if (!bio_flagged(bio, BIO_SEG_VALID))
		blk_recount_segments(q, bio);

	return ll_new_hw_segment(q, req, bio);
}

int ll_front_merge_fn(struct request_queue *q, struct request *req,
		      struct bio *bio)
{
	if (req_gap_front_merge(req, bio))
		return 0;
	if (blk_rq_sectors(req) + bio_sectors(bio) >
	    blk_rq_get_max_sectors(req)) {
		req_set_nomerge(q, req);
		return 0;
	}
	if (!bio_flagged(bio, BIO_SEG_VALID))
		blk_recount_segments(q, bio);
	if (!bio_flagged(req->bio, BIO_SEG_VALID))
		blk_recount_segments(q, req->bio);

	return ll_new_hw_segment(q, req, bio);
}

/*
 * blk-mq uses req->special to carry normal driver per-request payload, it
 * does not indicate a prepared command that we cannot merge with.
 */
static bool req_no_special_merge(struct request *req)
{
	struct request_queue *q = req->q;

	return !q->mq_ops && req->special;
}

static int ll_merge_requests_fn(struct request_queue *q, struct request *req,
				struct request *next)
{
	int total_phys_segments;
	unsigned int seg_size =
		req->biotail->bi_seg_back_size + next->bio->bi_seg_front_size;

	/*
	 * First check if the either of the requests are re-queued
	 * requests.  Can't merge them if they are.
	 */
	if (req_no_special_merge(req) || req_no_special_merge(next))
		return 0;

	if (req_gap_back_merge(req, next->bio))
		return 0;

	/*
	 * Will it become too large?
	 */
	if ((blk_rq_sectors(req) + blk_rq_sectors(next)) >
	    blk_rq_get_max_sectors(req))
		return 0;

	total_phys_segments = req->nr_phys_segments + next->nr_phys_segments;
	if (blk_phys_contig_segment(q, req->biotail, next->bio)) {
		if (req->nr_phys_segments == 1)
			req->bio->bi_seg_front_size = seg_size;
		if (next->nr_phys_segments == 1)
			next->biotail->bi_seg_back_size = seg_size;
		total_phys_segments--;
	}

	if (total_phys_segments > queue_max_segments(q))
		return 0;

	if (blk_integrity_rq(req) && blk_integrity_merge_rq(q, req, next))
		return 0;

	/* Merge is OK... */
	req->nr_phys_segments = total_phys_segments;
	return 1;
}

/**
 * blk_rq_set_mixed_merge - mark a request as mixed merge
 * @rq: request to mark as mixed merge
 *
 * Description:
 *     @rq is about to be mixed merged.  Make sure the attributes
 *     which can be mixed are set in each bio and mark @rq as mixed
 *     merged.
 */
void blk_rq_set_mixed_merge(struct request *rq)
{
	unsigned int ff = rq->cmd_flags & REQ_FAILFAST_MASK;
	struct bio *bio;

	if (rq->cmd_flags & REQ_MIXED_MERGE)
		return;

	/*
	 * @rq will no longer represent mixable attributes for all the
	 * contained bios.  It will just track those of the first one.
	 * Distributes the attributs to each bio.
	 */
	for (bio = rq->bio; bio; bio = bio->bi_next) {
		WARN_ON_ONCE((bio->bi_rw & REQ_FAILFAST_MASK) &&
			     (bio->bi_rw & REQ_FAILFAST_MASK) != ff);
		bio->bi_rw |= ff;
	}
	rq->cmd_flags |= REQ_MIXED_MERGE;
}

static void blk_account_io_merge(struct request *req)
{
	if (blk_do_io_stat(req)) {
		struct hd_struct *part;
		int cpu;

		cpu = part_stat_lock();
		part = req->part;

		part_round_stats(req->q, cpu, part);
		part_dec_in_flight(req->q, part, rq_data_dir(req));

		hd_struct_put(part);
		part_stat_unlock();
	}
}

/*
 * For non-mq, this has to be called with the request spinlock acquired.
 * For mq with scheduling, the appropriate queue wide lock should be held.
 */
static struct request *attempt_merge(struct request_queue *q,
				     struct request *req, struct request *next)
{
	if (!rq_mergeable(req) || !rq_mergeable(next))
		return NULL;

	if (!blk_check_merge_flags(req->cmd_flags, next->cmd_flags))
		return NULL;

	/*
	 * not contiguous
	 */
	if (blk_rq_pos(req) + blk_rq_sectors(req) != blk_rq_pos(next))
		return NULL;

	if (rq_data_dir(req) != rq_data_dir(next)
	    || req->rq_disk != next->rq_disk
	    || req_no_special_merge(next))
		return NULL;

	if (req->cmd_flags & REQ_WRITE_SAME &&
	    !blk_write_same_mergeable(req->bio, next->bio))
		return NULL;

	/*
	 * If we are allowed to merge, then append bio list
	 * from next to rq and release next. merge_requests_fn
	 * will have updated segment counts, update sector
	 * counts here.
	 */
	if (!ll_merge_requests_fn(q, req, next))
		return NULL;

	/*
	 * If failfast settings disagree or any of the two is already
	 * a mixed merge, mark both as mixed before proceeding.  This
	 * makes sure that all involved bios have mixable attributes
	 * set properly.
	 */
	if ((req->cmd_flags | next->cmd_flags) & REQ_MIXED_MERGE ||
	    (req->cmd_flags & REQ_FAILFAST_MASK) !=
	    (next->cmd_flags & REQ_FAILFAST_MASK)) {
		blk_rq_set_mixed_merge(req);
		blk_rq_set_mixed_merge(next);
	}

	/*
	 * At this point we have either done a back merge
	 * or front merge. We need the smaller start_time of
	 * the merged requests to be the current request
	 * for accounting purposes.
	 */
	if (time_after(req->start_time, next->start_time))
		req->start_time = next->start_time;

        /****hujunpeng test***************************************************/
        if(block_open_printk)
             printk("attempt_merge req:0x%p merge to req:0x%p\n",next,req);

	req->biotail->bi_next = next->bio;
	req->biotail = next->biotail;

	req->__data_len += blk_rq_bytes(next);

	elv_merge_requests(q, req, next);

	/*
	 * 'next' is going away, so update stats accordingly
	 */
	blk_account_io_merge(next);

	req->ioprio = ioprio_best(req->ioprio, next->ioprio);
	if (blk_rq_cpu_valid(next))
		req->cpu = next->cpu;
        
        /*如果next有高优先级属性，则清理掉，但要把高优先级属性传递到req，因为next合并到了req
        if(next->cmd_flags & REQ_HIGHPRIO){
            req->cmd_flags |= REQ_HIGHPRIO;---这段代码移动到了上方的 elv_merge_requests->deadline_merged_requests 函数里
            next->cmd_flags &= (~REQ_HIGHPRIO);
        }*/

	/*
	 * ownership of bio passed from next to req, return 'next' for
	 * the caller to free
	 */
	next->bio = NULL;
	return next;
}

struct request *attempt_back_merge(struct request_queue *q, struct request *rq)
{
	struct request *next = elv_latter_request(q, rq);

	if (next)
		return attempt_merge(q, rq, next);

	return NULL;
}

struct request *attempt_front_merge(struct request_queue *q, struct request *rq)
{
	struct request *prev = elv_former_request(q, rq);

	if (prev)
		return attempt_merge(q, prev, rq);

	return NULL;
}

int blk_attempt_req_merge(struct request_queue *q, struct request *rq,
			  struct request *next)
{
	struct elevator_queue *e = q->elevator;
	struct request *free;

	if (!e->uses_mq && e->aux->ops.sq.elevator_allow_rq_merge_fn)
		if (!e->aux->ops.sq.elevator_allow_rq_merge_fn(q, rq, next))
			return 0;

	free = attempt_merge(q, rq, next);
	if (free) {
		__blk_put_request(q, free);
		return 1;
	}

	return 0;
}

bool blk_rq_merge_ok(struct request *rq, struct bio *bio)
{
	if (!rq_mergeable(rq) || !bio_mergeable(bio))
		return false;

	if (!blk_check_merge_flags(rq->cmd_flags, bio->bi_rw))
		return false;

	/* different data direction or already started, don't merge */
	if (bio_data_dir(bio) != rq_data_dir(rq))
		return false;

	/* must be same device and not a special request */
	if (rq->rq_disk != bio->bi_bdev->bd_disk || req_no_special_merge(rq))
		return false;

	/* only merge integrity protected bio into ditto rq */
	if (bio_integrity(bio) != blk_integrity_rq(rq))
		return false;

	/* must be using the same buffer */
	if (rq->cmd_flags & REQ_WRITE_SAME &&
	    !blk_write_same_mergeable(rq->bio, bio))
		return false;

	return true;
}

int blk_try_merge(struct request *rq, struct bio *bio)
{
	if (blk_rq_pos(rq) + blk_rq_sectors(rq) == bio->bi_sector)
		return ELEVATOR_BACK_MERGE;
	else if (blk_rq_pos(rq) - bio_sectors(bio) == bio->bi_sector)
		return ELEVATOR_FRONT_MERGE;
	return ELEVATOR_NO_MERGE;
}
