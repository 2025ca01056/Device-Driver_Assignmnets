// SPDX-License-Identifier: GPL-2.0
/*
 * Slab-cache playground and kmalloc vs vmalloc allocator
 *              benchmark, driven by text commands from userspace.
 *
 * A private slab cache ("bits_rec") holds fixed 64-byte records.
 * Userspace controls allocation/free/benchmark via write() to
 * /dev/bitsmem with commands: "alloc N", "free N", "bench".
 * read() returns a text report of outstanding records, high-water
 * mark, cache object size, and the latest benchmark timings.
 *
 * Assignment 2, Question 2 (ch11-bitsmem)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/kstrtox.h>

#define DRV_NAME	"bitsmem"
#define REC_SIZE	64
#define REPORT_BUF_SZ	512
#define CMD_BUF_SZ	64
#define BENCH_ITERS	100

struct bits_rec {
	struct list_head list;
	u8 payload[REC_SIZE - sizeof(struct list_head)];
};

struct bench_result {
	s64 kmalloc_64k_ns;
	s64 vmalloc_64k_ns;
	s64 kmalloc_1m_ns;
	s64 vmalloc_1m_ns;
	bool valid;
};

static struct {
	struct kmem_cache	*cache;

	struct mutex		lock;		/* protects list + counters */
	struct list_head	rec_list;
	u64			outstanding;
	u64			high_water;

	struct bench_result	bench;

	struct miscdevice	miscdev;
} bmem;

/*
 * -------------------------------------------------------------------
 * alloc / free helpers (caller holds bmem.lock)
 * -------------------------------------------------------------------
 */
static int bitsmem_alloc_n(u64 n)
{
	u64 i;
	struct bits_rec *r;
	LIST_HEAD(batch);

	if (n > 5000000)   /* refuse unreasonably large single requests */
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		r = kmem_cache_alloc(bmem.cache, GFP_KERNEL);
		if (!r) {
			/* Roll back the partial batch on failure. */
			struct bits_rec *tmp, *pos;

			list_for_each_entry_safe(pos, tmp, &batch, list) {
				list_del(&pos->list);
				kmem_cache_free(bmem.cache, pos);
			}
			return -ENOMEM;
		}
		list_add_tail(&r->list, &batch);
	}

	list_splice_tail(&batch, &bmem.rec_list);
	bmem.outstanding += n;
	if (bmem.outstanding > bmem.high_water)
		bmem.high_water = bmem.outstanding;

	return 0;
}

static int bitsmem_free_n(u64 n)
{
	struct bits_rec *r, *tmp;
	u64 freed = 0;

	if (n > bmem.outstanding)
		n = bmem.outstanding;

	list_for_each_entry_safe(r, tmp, &bmem.rec_list, list) {
		if (freed >= n)
			break;
		list_del(&r->list);
		kmem_cache_free(bmem.cache, r);
		freed++;
	}

	bmem.outstanding -= freed;
	return 0;
}

static void bitsmem_free_all_locked(void)
{
	struct bits_rec *r, *tmp;

	list_for_each_entry_safe(r, tmp, &bmem.rec_list, list) {
		list_del(&r->list);
		kmem_cache_free(bmem.cache, r);
	}
	bmem.outstanding = 0;
}

/*
 * -------------------------------------------------------------------
 * Benchmark: kmalloc vs vmalloc, alloc+free cost, averaged.
 * -------------------------------------------------------------------
 */
static s64 bench_kmalloc(size_t size, unsigned int iters)
{
	unsigned int i;
	s64 start, total = 0;
	void *p;

	for (i = 0; i < iters; i++) {
		start = ktime_get_ns();
		p = kmalloc(size, GFP_KERNEL);
		kfree(p);
		total += ktime_get_ns() - start;
	}
	return total / iters;
}

static s64 bench_vmalloc(size_t size, unsigned int iters)
{
	unsigned int i;
	s64 start, total = 0;
	void *p;

	for (i = 0; i < iters; i++) {
		start = ktime_get_ns();
		p = vmalloc(size);
		vfree(p);
		total += ktime_get_ns() - start;
	}
	return total / iters;
}

static void bitsmem_run_bench(void)
{
	bmem.bench.kmalloc_64k_ns = bench_kmalloc(64 * 1024, BENCH_ITERS);
	bmem.bench.vmalloc_64k_ns = bench_vmalloc(64 * 1024, BENCH_ITERS);
	bmem.bench.kmalloc_1m_ns  = bench_kmalloc(1024 * 1024, BENCH_ITERS);
	bmem.bench.vmalloc_1m_ns  = bench_vmalloc(1024 * 1024, BENCH_ITERS);
	bmem.bench.valid = true;

	pr_info("%s: bench 64K kmalloc=%lldns vmalloc=%lldns | 1M kmalloc=%lldns vmalloc=%lldns\n",
		DRV_NAME, bmem.bench.kmalloc_64k_ns, bmem.bench.vmalloc_64k_ns,
		bmem.bench.kmalloc_1m_ns, bmem.bench.vmalloc_1m_ns);
}

/*
 * -------------------------------------------------------------------
 * /dev/bitsmem file operations
 * -------------------------------------------------------------------
 */
static ssize_t bitsmem_write(struct file *filp, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	char cmd[CMD_BUF_SZ];
	char *p, *tok;
	u64 n;
	int ret;

	if (count == 0 || count >= sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(cmd, ubuf, count))
		return -EFAULT;
	cmd[count] = '\0';

	/* Strip trailing newline, if any (echo/tee add one). */
	p = strim(cmd);

	tok = strsep(&p, " \t");
	if (!tok)
		return -EINVAL;

	mutex_lock(&bmem.lock);

	if (strcmp(tok, "alloc") == 0) {
		if (!p || kstrtou64(strim(p), 10, &n)) {
			ret = -EINVAL;
			goto out;
		}
		ret = bitsmem_alloc_n(n);
		if (ret == 0)
			ret = count;
	} else if (strcmp(tok, "free") == 0) {
		if (!p || kstrtou64(strim(p), 10, &n)) {
			ret = -EINVAL;
			goto out;
		}
		ret = bitsmem_free_n(n);
		if (ret == 0)
			ret = count;
	} else if (strcmp(tok, "bench") == 0) {
		bitsmem_run_bench();
		ret = count;
	} else {
		ret = -EINVAL;
	}

out:
	mutex_unlock(&bmem.lock);
	return ret;
}

static ssize_t bitsmem_read(struct file *filp, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	char buf[REPORT_BUF_SZ];
	int len;

	if (*ppos > 0)
		return 0;

	mutex_lock(&bmem.lock);
	len = scnprintf(buf, sizeof(buf),
		"outstanding=%llu hwm=%llu obj_size=%u\n"
		"bench_valid=%d\n"
		"kmalloc_64K_ns=%lld vmalloc_64K_ns=%lld\n"
		"kmalloc_1M_ns=%lld vmalloc_1M_ns=%lld\n",
		bmem.outstanding, bmem.high_water,
		kmem_cache_size(bmem.cache),
		bmem.bench.valid,
		bmem.bench.kmalloc_64k_ns, bmem.bench.vmalloc_64k_ns,
		bmem.bench.kmalloc_1m_ns, bmem.bench.vmalloc_1m_ns);
	mutex_unlock(&bmem.lock);

	if (copy_to_user(ubuf, buf, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

static const struct file_operations bitsmem_fops = {
	.owner	= THIS_MODULE,
	.read	= bitsmem_read,
	.write	= bitsmem_write,
};

/*
 * -------------------------------------------------------------------
 * Module init / exit
 * -------------------------------------------------------------------
 */
static void bits_rec_ctor(void *obj)
{
	/* no-op constructor; its only purpose is to prevent SLUB from
	 * merging this cache with other same-size caches, so it shows
	 * up under its own name "bits_rec" in /proc/slabinfo.
	 */
}

static int __init bitsmem_init(void)
{
	int ret;

	mutex_init(&bmem.lock);
	INIT_LIST_HEAD(&bmem.rec_list);
	bmem.outstanding = 0;
	bmem.high_water = 0;
	memset(&bmem.bench, 0, sizeof(bmem.bench));

	bmem.cache = kmem_cache_create("bits_rec", sizeof(struct bits_rec),
					0, SLAB_HWCACHE_ALIGN, bits_rec_ctor);
	if (!bmem.cache) {
		pr_err("%s: kmem_cache_create failed\n", DRV_NAME);
		return -ENOMEM;
	}

	bmem.miscdev.minor = MISC_DYNAMIC_MINOR;
	bmem.miscdev.name  = DRV_NAME;
	bmem.miscdev.fops  = &bitsmem_fops;

	ret = misc_register(&bmem.miscdev);
	if (ret) {
		pr_err("%s: misc_register failed: %d\n", DRV_NAME, ret);
		kmem_cache_destroy(bmem.cache);
		return ret;
	}

	pr_info("%s: module loaded, cache obj_size=%u, /dev/%s ready\n",
		DRV_NAME, kmem_cache_size(bmem.cache), DRV_NAME);
	return 0;
}

static void __exit bitsmem_exit(void)
{
	misc_deregister(&bmem.miscdev);

	mutex_lock(&bmem.lock);
	bitsmem_free_all_locked();
	mutex_unlock(&bmem.lock);

	kmem_cache_destroy(bmem.cache);

	pr_info("%s: module unloaded\n", DRV_NAME);
}

module_init(bitsmem_init);
module_exit(bitsmem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>");
MODULE_DESCRIPTION("Slab-cache playground and kmalloc vs vmalloc allocator benchmark (bitsmem)");
