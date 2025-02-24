/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_LINUX__
#define __XFS_LINUX__

#include <linux/types.h>
#include <linux/uuid.h>

/*
 * Kernel specific type declarations for XFS
 */

typedef __s64			xfs_off_t;	/* <file offset> type */
typedef unsigned long long	xfs_ino_t;	/* <inode> type */
typedef __s64			xfs_daddr_t;	/* <disk address> type */
typedef __u32			xfs_dev_t;
typedef __u32			xfs_nlink_t;

typedef uuid_be			uuid_t;

#include "xfs_types.h"

#include "kmem.h"
#include "mrlock.h"

#include <linux/semaphore.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/crc32c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/major.h>
#include <linux/pagemap.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/sort.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/ctype.h>
#include <linux/writeback.h>
#include <linux/capability.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/list_sort.h>
#include <linux/ratelimit.h>

#include <asm/page.h>
#include <asm/div64.h>
#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include "xfs_fs.h"
#include "xfs_stats.h"
#include "xfs_sysctl.h"
#include "xfs_iops.h"
#include "xfs_aops.h"
#include "xfs_super.h"
#include "xfs_cksum.h"
#include "xfs_buf.h"
#include "xfs_message.h"

#ifdef __BIG_ENDIAN
#define XFS_NATIVE_HOST 1
#else
#undef XFS_NATIVE_HOST
#endif

#define irix_sgid_inherit	xfs_params.sgid_inherit.val
#define irix_symlink_mode	xfs_params.symlink_mode.val
#define xfs_panic_mask		xfs_params.panic_mask.val
#define xfs_error_level		xfs_params.error_level.val
#define xfs_syncd_centisecs	xfs_params.syncd_timer.val
#define xfs_stats_clear		xfs_params.stats_clear.val
#define xfs_inherit_sync	xfs_params.inherit_sync.val
#define xfs_inherit_nodump	xfs_params.inherit_nodump.val
#define xfs_inherit_noatime	xfs_params.inherit_noatim.val
#define xfs_inherit_nosymlinks	xfs_params.inherit_nosym.val
#define xfs_rotorstep		xfs_params.rotorstep.val
#define xfs_inherit_nodefrag	xfs_params.inherit_nodfrg.val
#define xfs_fstrm_centisecs	xfs_params.fstrm_timer.val
#define xfs_eofb_secs		xfs_params.eofb_timer.val

#define current_cpu()		(raw_smp_processor_id())
#define current_pid()		(current->pid)
#define current_test_flags(f)	(current->flags & (f))
#define current_set_flags_nested(sp, f)		\
		(*(sp) = current->flags, current->flags |= (f))
#define current_clear_flags_nested(sp, f)	\
		(*(sp) = current->flags, current->flags &= ~(f))
#define current_restore_flags_nested(sp, f)	\
		(current->flags = ((current->flags & ~(f)) | (*(sp) & (f))))

#define spinlock_destroy(lock)

#define NBBY		8		/* number of bits per byte */

/*
 * Size of block device i/o is parameterized here.
 * Currently the system supports page-sized i/o.
 */
#define	BLKDEV_IOSHIFT		PAGE_CACHE_SHIFT
#define	BLKDEV_IOSIZE		(1<<BLKDEV_IOSHIFT)
/* number of BB's per block device block */
#define	BLKDEV_BB		BTOBB(BLKDEV_IOSIZE)

#define ENOATTR		ENODATA		/* Attribute not found */
#define EWRONGFS	EINVAL		/* Mount with wrong filesystem type */
#define EFSCORRUPTED	EUCLEAN		/* Filesystem is corrupted */
#define EFSBADCRC	EBADMSG		/* Bad CRC detected */

#define SYNCHRONIZE()	barrier()
#define __return_address __builtin_return_address(0)

#define XFS_PROJID_DEFAULT	0

#define MIN(a,b)	(min(a,b))
#define MAX(a,b)	(max(a,b))
#define howmany(x, y)	(((x)+((y)-1))/(y))

static inline void delay(long ticks)
{
	schedule_timeout_uninterruptible(ticks);
}

/*
 * XFS wrapper structure for sysfs support. It depends on external data
 * structures and is embedded in various internal data structures to implement
 * the XFS sysfs object heirarchy. Define it here for broad access throughout
 * the codebase.
 */
struct xfs_kobj {
	struct kobject		kobject;
	struct completion	complete;
};

struct xstats {
	struct xfsstats __percpu	*xs_stats;
	struct xfs_kobj			xs_kobj;
};

extern struct xstats xfsstats;

/* Kernel uid/gid conversion. These are used to convert to/from the on disk
 * uid_t/gid_t types to the kuid_t/kgid_t types that the kernel uses internally.
 * The conversion here is type only, the value will remain the same since we
 * are converting to the init_user_ns. The uid is later mapped to a particular
 * user namespace value when crossing the kernel/user boundary.
 */
static inline uint32_t xfs_kuid_to_uid(kuid_t uid)
{
	return from_kuid(&init_user_ns, uid);
}

static inline kuid_t xfs_uid_to_kuid(uint32_t uid)
{
	return make_kuid(&init_user_ns, uid);
}

static inline uint32_t xfs_kgid_to_gid(kgid_t gid)
{
	return from_kgid(&init_user_ns, gid);
}

static inline kgid_t xfs_gid_to_kgid(uint32_t gid)
{
	return make_kgid(&init_user_ns, gid);
}

/*
 * Various platform dependent calls that don't fit anywhere else
 */
#define xfs_sort(a,n,s,fn)	sort(a,n,s,fn,NULL)
#define xfs_stack_trace()	dump_stack()

/* Side effect free 64 bit mod operation */
static inline __u32 xfs_do_mod(void *a, __u32 b, int n)
{
	switch (n) {
		case 4:
			return *(__u32 *)a % b;
		case 8:
			{
			__u64	c = *(__u64 *)a;
			return do_div(c, b);
			}
	}

	/* NOTREACHED */
	return 0;
}

#define do_mod(a, b)	xfs_do_mod(&(a), (b), sizeof(a))

static inline uint64_t roundup_64(uint64_t x, uint32_t y)
{
	x += y - 1;
	do_div(x, y);
	return x * y;
}

static inline uint64_t howmany_64(uint64_t x, uint32_t y)
{
	x += y - 1;
	do_div(x, y);
	return x;
}

#define ASSERT_ALWAYS(expr)	\
	(likely(expr) ? (void)0 : assfail(#expr, __FILE__, __LINE__))

#ifdef DEBUG
#define ASSERT(expr)	\
	(likely(expr) ? (void)0 : assfail(#expr, __FILE__, __LINE__))

#ifndef STATIC
# define STATIC noinline
#endif

#else	/* !DEBUG */

#ifdef XFS_WARN

#define ASSERT(expr)	\
	(likely(expr) ? (void)0 : asswarn(#expr, __FILE__, __LINE__))

#ifndef STATIC
# define STATIC static noinline
#endif

#else	/* !DEBUG && !XFS_WARN */

#define ASSERT(expr)	((void)0)

#ifndef STATIC
# define STATIC static noinline
#endif

#endif /* XFS_WARN */
#endif /* DEBUG */

#ifdef CONFIG_XFS_RT
#define XFS_IS_REALTIME_INODE(ip) ((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME)
#else
#define XFS_IS_REALTIME_INODE(ip) (0)
#endif

#endif /* __XFS_LINUX__ */
