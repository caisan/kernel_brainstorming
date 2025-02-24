/*
 *  Deadline i/o scheduler.
 *
 *  Copyright (C) 2002 Jens Axboe <axboe@kernel.dk>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>

/******hujunpeng test*****************************************/
extern void elv_dispatch_add_head(struct request_queue *q, struct request *rq);

/*
 * See Documentation/block/deadline-iosched.txt
 */
static const int read_expire = HZ / 2;  /* max time before a read is submitted. */
static const int write_expire = 5 * HZ; /* ditto for writes, these limits are SOFT! */
static const int writes_starved = 2;    /* max times reads can starve a write */
static const int fifo_batch = 16;       /* # of sequential requests treated as one
				     by the above parameters. For throughput. */

struct deadline_data {
	/*
	 * run time data
	 */

	/*
	 * requests (deadline_rq s) are present on both sort_list and fifo_list
	 */
	struct rb_root sort_list[2];	
	struct list_head fifo_list[2];

	/*
	 * next in sort order. read, write or both are NULL
	 */
	struct request *next_rq[2];
	unsigned int batching;		/* number of sequential requests made */
	sector_t last_sector;		/* head position */
	unsigned int starved;		/* times reads have starved writes */

	/*
	 * settings that change how the i/o scheduler behaves
	 */
	int fifo_expire[2];
	int fifo_batch;
	int writes_starved;
	int front_merges;
};

static void deadline_move_request(struct deadline_data *, struct request *);

static inline struct rb_root *
deadline_rb_root(struct deadline_data *dd, struct request *rq)
{
	return &dd->sort_list[rq_data_dir(rq)];
}

/*
 * get the request after `rq' in sector-sorted order
 */
static inline struct request *
deadline_latter_request(struct request *rq)
{
	struct rb_node *node = rb_next(&rq->rb_node);

	if (node)
		return rb_entry_rq(node);

	return NULL;
}

static void
deadline_add_rq_rb(struct deadline_data *dd, struct request *rq)
{
	struct rb_root *root = deadline_rb_root(dd, rq);

	elv_rb_add(root, rq);
}

static inline void
deadline_del_rq_rb(struct deadline_data *dd, struct request *rq)
{
	const int data_dir = rq_data_dir(rq);

	if (dd->next_rq[data_dir] == rq)
		dd->next_rq[data_dir] = deadline_latter_request(rq);

	elv_rb_del(deadline_rb_root(dd, rq), rq);
}

/*
 * add rq to rbtree and fifo
 */
static void
deadline_add_request(struct request_queue *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq);

	deadline_add_rq_rb(dd, rq);

	/*
	 * set expire time and add to fifo list
	 */
        //如果req有高优先级传输属性，deadline算法把req添加到fifo队列，添加到红黑树队列在上边的deadline_add_rq_rb()
        if(rq->cmd_flags & REQ_HIGHPRIO){
	    rq->fifo_time = jiffies;//如果req有高优先级传输属性，则req放入fifo链表头，超时时间0，保证最快被调度派发给驱动
	    list_add(&rq->queuelist, &dd->fifo_list[data_dir]);
            printk("deadline_add_request high prio req:0x%p\n",rq);
        }else{
	    rq->fifo_time = jiffies + dd->fifo_expire[data_dir];
	    list_add_tail(&rq->queuelist, &dd->fifo_list[data_dir]);
        }
}

/*
 * remove rq from rbtree and fifo.
 */
static void deadline_remove_request(struct request_queue *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator->elevator_data;

	rq_fifo_clear(rq);
	deadline_del_rq_rb(dd, rq);
}

static int
deadline_merge(struct request_queue *q, struct request **req, struct bio *bio)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct request *__rq;
	int ret;

	/*
	 * check for front merge
	 */
	if (dd->front_merges) {
		sector_t sector = bio_end_sector(bio);

		__rq = elv_rb_find(&dd->sort_list[bio_data_dir(bio)], sector);
		if (__rq) {
			BUG_ON(sector != blk_rq_pos(__rq));

			if (elv_bio_merge_ok(__rq, bio)) {
				ret = ELEVATOR_FRONT_MERGE;
				goto out;
			}
		}
	}

	return ELEVATOR_NO_MERGE;
out:
	*req = __rq;
	return ret;
}

static void deadline_merged_request(struct request_queue *q,
				    struct request *req, int type)
{
	struct deadline_data *dd = q->elevator->elevator_data;

	/*
	 * if the merge was a front merge, we need to reposition request
	 */
	if (type == ELEVATOR_FRONT_MERGE) {
		elv_rb_del(deadline_rb_root(dd, req), req);
		deadline_add_rq_rb(dd, req);
	}
}

static void
deadline_merged_requests(struct request_queue *q, struct request *req,
			 struct request *next)
{
	/*
	 * if next expires before rq, assign its expire time to rq
	 * and move into next position (next will be deleted) in fifo
	 */
	if (!list_empty(&req->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before(next->fifo_time, req->fifo_time)) {
			list_move(&req->queuelist, &next->queuelist);
			req->fifo_time = next->fifo_time;
		}
	}

	/*
	 * kill knowledge of next, this one is a goner
	 */
	deadline_remove_request(q, next);
        
        //next合并到req，next的高优先级传递到req,并且req要放到fifo队列头，会得到优先派发的机会 
        if(next->cmd_flags & REQ_HIGHPRIO){
	    struct deadline_data *dd = q->elevator->elevator_data;
	    const int data_dir = rq_data_dir(req);

	    req->fifo_time = jiffies;//req放入fifo链表头，超时时间0，保证最快被调度派发给驱动
            req->cmd_flags |= REQ_HIGHPRIO;//设置req高优先级
            /*因为req本身就在dd->fifo_list[data_dir]，必须使用list_move:把req先从dd->fifo_list[data_dir]删除掉再添加到dd->fifo_list[data_dir]链表头*/
	    //list_add(&req->queuelist, &dd->fifo_list[data_dir]);
            list_move(&req->queuelist, &dd->fifo_list[data_dir]);

            //如果next这个req有高优先级传输属性，必须清理掉，它不会参与IO传输，这是唯一清理高优先级属性的机会
            next->cmd_flags &= ~REQ_HIGHPRIO;
            printk("deadline_merged_requests high prio req:0x%p\n",req);
        }
}

/*
 * move request from sort list to dispatch queue.
 */
static inline void
deadline_move_to_dispatch(struct deadline_data *dd, struct request *rq)
{
	struct request_queue *q = rq->q;

	deadline_remove_request(q, rq);

        //这里有个漏洞，也有可能是其他进程派发test进程提交的req，不能以进程限制
        //if(strcmp(current->comm,"test") == 0)
        //如果req有高优先级传输属性，则要把req加入q->queue_head链表头，这样该req会得到优先派发
        if(rq->cmd_flags & REQ_HIGHPRIO)
	    elv_dispatch_add_head(q, rq);
        else
	    elv_dispatch_add_tail(q, rq);
}

/*
 * move an entry to dispatch queue
 */
static void
deadline_move_request(struct deadline_data *dd, struct request *rq)
{
	const int data_dir = rq_data_dir(rq);

	dd->next_rq[READ] = NULL;
	dd->next_rq[WRITE] = NULL;
	dd->next_rq[data_dir] = deadline_latter_request(rq);

	dd->last_sector = rq_end_sector(rq);

	/*
	 * take it off the sort and fifo list, move
	 * to dispatch queue
	 */
	deadline_move_to_dispatch(dd, rq);
}

/*
 * deadline_check_fifo returns 0 if there are no expired requests on the fifo,
 * 1 otherwise. Requires !list_empty(&dd->fifo_list[data_dir])
 */
static inline int deadline_check_fifo(struct deadline_data *dd, int ddir)
{
	struct request *rq = rq_entry_fifo(dd->fifo_list[ddir].next);

	/*
	 * rq is expired!
	 */
	if (time_after_eq(jiffies, rq->fifo_time))
		return 1;

	return 0;
}

/*
 * deadline_dispatch_requests selects the best request according to
 * read/write expire, fifo_batch, etc
 */
static int deadline_dispatch_requests(struct request_queue *q, int force)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const int reads = !list_empty(&dd->fifo_list[READ]);
	const int writes = !list_empty(&dd->fifo_list[WRITE]);
	struct request *rq;
	int data_dir;

        /*取出fifo队列头的read/write req*/
        struct request *r_req = rq_entry_fifo(dd->fifo_list[READ].next);
        struct request *w_req = rq_entry_fifo(dd->fifo_list[WRITE].next);
        #define IS_REQ_HIGHPRIO(req) ((req->cmd_flags & REQ_IO_STAT) && (req->cmd_type == REQ_TYPE_FS) && (req->cmd_flags&REQ_HIGHPRIO))
	/*
	 * batches are currently reads XOR writes
	 */
	if (dd->next_rq[WRITE])
		rq = dd->next_rq[WRITE];
	else
		rq = dd->next_rq[READ];
        
        /*
         *如果fifo队列头的req有高优先级传输属性,直接派发dd->fifo_list[].next队列头的这个req。还要把rq=dd->next_rq[]这个req放到dd->fifo_list[].next后边。为什么呢?
         *该函数后边执行deadline_move_request()会把dd->next_rq[]在rb tree的下一个req赋值于dd->next_rq[]，这样dd->next_rq[]原本
         *保存的req就要消失了，得不得传输的机会!!!相当于这个req暂时丢失了,错，不会丢失，因为dd->next_rq[]（称为req_next)保存的req始终在fifo队列和rb tree，
         *总会得到被选中派发的机会.这样操作后，该函数最后会执行deadline_move_requesti()再把dd->fifo_list[].next后边的req(即req_next)添加到dd->next_rq[]，
         *下次传输优先传这个req。实际派发req的进程未必是test进程，也有可能是其他IO进程，所以要去除(strcmp(current->comm,"test") == 0)的限制
        */
        if(rq && ((w_req && IS_REQ_HIGHPRIO(w_req)) || (r_req && IS_REQ_HIGHPRIO(r_req)))){
            //rq不能和r_req是同一个req，因为同一个没必要特殊处理。并且dd->next_rq[READ]就是rq，这是判断rq是读req还是写req，rq和r_req是都是一个读属性才行
            //fifo队列分读写req链表,读写req要分开处理
        #if 0
            if(r_req && (rq != r_req) && (r_req->cmd_flags & REQ_HIGHPRIO) && dd->next_rq[READ])---这个判断不充分，用IS_REQ_HIGHPRIO()充分
                list_move(&rq->queuelist, &r_req->queuelist);
            else if(w_req && (rq != w_req) && (w_req->cmd_flags & REQ_HIGHPRIO) && dd->next_rq[WRITE])
                list_move(&rq->queuelist, &w_req->queuelist);
        #else
            if(w_req && IS_REQ_HIGHPRIO(w_req))
                printk("%s w_req->cmd_flags:0x%llx w_req:0x%p rq:0x%p\n",__func__,w_req->cmd_flags&REQ_HIGHPRIO,w_req,rq);
            if(r_req && IS_REQ_HIGHPRIO(r_req))
                printk("%s r_req->cmd_flags:0x%llx r_req:0x%p rq:0x%p\n",__func__,r_req->cmd_flags&REQ_HIGHPRIO,r_req,rq);

            //加上 IS_REQ_HIGHPRIO 判断是因为测试时总是发现有一个异常req->cmd_flags=0x1000001388，这种异常req要过滤掉
            if(w_req && (rq != w_req) && IS_REQ_HIGHPRIO(w_req) && dd->next_rq[WRITE])//要先判断write req，因为前边rq是优先赋值rq = dd->next_rq[WRITE]
                list_move(&rq->queuelist, &w_req->queuelist);
            else if(r_req && (rq != r_req) && IS_REQ_HIGHPRIO(r_req) && dd->next_rq[READ])
                list_move(&rq->queuelist, &r_req->queuelist);
        #endif

            //直接跳到dispatch_find_request分支派发fifo队列头高优先级属性的req，data_dir=WRITE是为了优先派发fifo队列头write属性的高优先级属性的req
            //如果rq(即dd->next_rq[WRITE])==w_req 或者 rq(dd->next_rq[READ])==r_req,那也goto dispatch_find_request派发fifo队列头的高优先级属性req,都是同一个req
            if(w_req && IS_REQ_HIGHPRIO(w_req))
                data_dir = WRITE;
            else
                data_dir = READ;

            goto  dispatch_find_request;
        }
        else if (rq && dd->batching < dd->fifo_batch)
            /* we have a next request are still entitled to batch */
	    goto dispatch_request;

	/*
	 * at this point we are not running a batch. select the appropriate
	 * data direction (read / write)
	 */

	if (reads) {
		BUG_ON(RB_EMPTY_ROOT(&dd->sort_list[READ]));

		if (writes && (dd->starved++ >= dd->writes_starved))
			goto dispatch_writes;

		data_dir = READ;

		goto dispatch_find_request;
	}

	/*
	 * there are either no reads or writes have been starved
	 */

	if (writes) {
dispatch_writes:
		BUG_ON(RB_EMPTY_ROOT(&dd->sort_list[WRITE]));

		dd->starved = 0;

		data_dir = WRITE;

		goto dispatch_find_request;
	}

	return 0;

dispatch_find_request:
	/*
	 * we are not running a batch, find best request for selected data_dir
	 */
	if (deadline_check_fifo(dd, data_dir) || !dd->next_rq[data_dir]) {
		/*
		 * A deadline has expired, the last request was in the other
		 * direction, or we have run out of higher-sectored requests.
		 * Start again from the request with the earliest expiry time.
		 */
		rq = rq_entry_fifo(dd->fifo_list[data_dir].next);
	} else {
		/*
		 * The last req was the same dir and we have a next request in
		 * sort order. No expired requests so continue on from here.
		 */
		rq = dd->next_rq[data_dir];
	}

	dd->batching = 0;

dispatch_request:
	/*
	 * rq is the selected appropriate request.
	 */
	dd->batching++;
	deadline_move_request(dd, rq);

	return 1;
}

static void deadline_exit_queue(struct elevator_queue *e)
{
	struct deadline_data *dd = e->elevator_data;

	BUG_ON(!list_empty(&dd->fifo_list[READ]));
	BUG_ON(!list_empty(&dd->fifo_list[WRITE]));

	kfree(dd);
}

/*
 * initialize elevator private data (deadline_data).
 */
static int deadline_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct deadline_data *dd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	dd = kmalloc_node(sizeof(*dd), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!dd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = dd;

	INIT_LIST_HEAD(&dd->fifo_list[READ]);
	INIT_LIST_HEAD(&dd->fifo_list[WRITE]);
	dd->sort_list[READ] = RB_ROOT;
	dd->sort_list[WRITE] = RB_ROOT;
	dd->fifo_expire[READ] = read_expire;
	dd->fifo_expire[WRITE] = write_expire;
	dd->writes_starved = writes_starved;
	dd->front_merges = 1;
	dd->fifo_batch = fifo_batch;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

/*
 * sysfs parts below
 */

static ssize_t
deadline_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
deadline_var_store(int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtol(p, &p, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data = __VAR;						\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return deadline_var_show(__data, (page));			\
}
SHOW_FUNCTION(deadline_read_expire_show, dd->fifo_expire[READ], 1);
SHOW_FUNCTION(deadline_write_expire_show, dd->fifo_expire[WRITE], 1);
SHOW_FUNCTION(deadline_writes_starved_show, dd->writes_starved, 0);
SHOW_FUNCTION(deadline_front_merges_show, dd->front_merges, 0);
SHOW_FUNCTION(deadline_fifo_batch_show, dd->fifo_batch, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data;							\
	int ret = deadline_var_store(&__data, (page), count);		\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	if (__CONV)							\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else								\
		*(__PTR) = __data;					\
	return ret;							\
}
STORE_FUNCTION(deadline_read_expire_store, &dd->fifo_expire[READ], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_write_expire_store, &dd->fifo_expire[WRITE], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_writes_starved_store, &dd->writes_starved, INT_MIN, INT_MAX, 0);
STORE_FUNCTION(deadline_front_merges_store, &dd->front_merges, 0, 1, 0);
STORE_FUNCTION(deadline_fifo_batch_store, &dd->fifo_batch, 0, INT_MAX, 0);
#undef STORE_FUNCTION

#define DD_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, deadline_##name##_show, \
				      deadline_##name##_store)

static struct elv_fs_entry deadline_attrs[] = {
	DD_ATTR(read_expire),
	DD_ATTR(write_expire),
	DD_ATTR(writes_starved),
	DD_ATTR(front_merges),
	DD_ATTR(fifo_batch),
	__ATTR_NULL
};

static struct elevator_type iosched_deadline = {
	.ops = {
		.elevator_merge_fn = 		deadline_merge,
		.elevator_merged_fn =		deadline_merged_request,
		.elevator_merge_req_fn =	deadline_merged_requests,
		.elevator_dispatch_fn =		deadline_dispatch_requests,
		.elevator_add_req_fn =		deadline_add_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_init_fn =		deadline_init_queue,
		.elevator_exit_fn =		deadline_exit_queue,
	},

	.elevator_attrs = deadline_attrs,
	.elevator_name = "deadline",
	.elevator_owner = THIS_MODULE,
};

static int __init deadline_init(void)
{
	return elv_register(&iosched_deadline);
}

static void __exit deadline_exit(void)
{
	elv_unregister(&iosched_deadline);
}

module_init(deadline_init);
module_exit(deadline_exit);

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("deadline IO scheduler");
