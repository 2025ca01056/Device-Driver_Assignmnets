// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel-thread sampler with workqueue-based deferred
 *              statistics processing.
 *
 * A kthread ("bitsmon-worker") appends timestamped sample records to a
 * spinlock-protected list at a configurable interval. A delayed work
 * item drains that list every 5 seconds, computes min/max/average
 * inter-sample gap, and stores the result in a mutex-protected stats
 * structure. Userspace can read the latest stats via /dev/bitsmon and
 * reconfigure the sampling interval via the sysfs attribute
 * /sys/class/bitsmon/bitsmon/interval_ms.
 *
 * Assignment 2, Question 1 (ch09-bitsmon)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/kstrtox.h>

#define DRV_NAME		"bitsmon"
#define STATS_PERIOD_MS		5000
#define INTERVAL_MIN_MS		100
#define INTERVAL_MAX_MS		5000
#define INTERVAL_DEFAULT_MS	1000
#define STATS_BUF_SZ		256

struct bitsmon_sample {
	struct list_head	list;
	u64			seq;
	unsigned long		jiffies_at;
	s64			ns;
};

struct bitsmon_stats {
	struct mutex	lock;
	u64		n;		/* samples processed in last window */
	u64		total_samples;	/* lifetime */
	s64		min_gap_ms;
	s64		max_gap_ms;
	s64		avg_gap_ms;
	bool		valid;
};

static struct {
	struct task_struct	*thread;

	spinlock_t		list_lock;
	struct list_head	sample_list;
	u64			seq;

	struct delayed_work	stats_work;
	struct bitsmon_stats	stats;

	atomic_t		interval_ms;

	struct miscdevice	miscdev;
} bm;

/*
 * -------------------------------------------------------------------
 * Producer: kernel thread
 * -------------------------------------------------------------------
 */
static int bitsmon_thread_fn(void *data)
{
	pr_info("%s: bitsmon-worker started (pid=%d)\n", DRV_NAME,
		current->pid);

	while (!kthread_should_stop()) {
		struct bitsmon_sample *s;
		unsigned long flags;
		unsigned int interval;

		s = kmalloc(sizeof(*s), GFP_KERNEL);
		if (!s) {
			/* Skip this tick rather than oops; try again later */
			msleep_interruptible(INTERVAL_MIN_MS);
			continue;
		}

		s->jiffies_at = jiffies;
		s->ns = ktime_get_ns();

		spin_lock_irqsave(&bm.list_lock, flags);
		s->seq = bm.seq++;
		list_add_tail(&s->list, &bm.sample_list);
		spin_unlock_irqrestore(&bm.list_lock, flags);

		interval = atomic_read(&bm.interval_ms);
		/*
		 * msleep_interruptible() returns immediately (0 or the
		 * remaining time) if a signal or kthread_stop() wakes us
		 * up, so the shutdown latency is bounded by one interval
		 * at most and the loop condition re-checks promptly.
		 */
		msleep_interruptible(interval);
	}

	pr_info("%s: bitsmon-worker exiting\n", DRV_NAME);
	return 0;
}

/*
 * -------------------------------------------------------------------
 * Consumer: delayed work
 *
 * Design note (see README for the full justification): the spinlock
 * protects ONLY the shared list_head. We never sleep or do heavy
 * computation while holding it. Instead, the work handler splices the
 * entire producer list onto a private, off-list local list under the
 * lock, drops the lock immediately, and processes the private list
 * with no lock held at all. This keeps the spinlock critical section
 * O(1) regardless of how many samples have queued up.
 * -------------------------------------------------------------------
 */
static void bitsmon_work_fn(struct work_struct *work)
{
	struct list_head priv_list;
	struct bitsmon_sample *s, *tmp;
	unsigned long flags;
	s64 prev_ns = 0;
	bool have_prev = false;
	s64 min_gap = S64_MAX, max_gap = 0, sum_gap = 0;
	u64 n = 0;

	INIT_LIST_HEAD(&priv_list);

	spin_lock_irqsave(&bm.list_lock, flags);
	list_splice_init(&bm.sample_list, &priv_list);
	spin_unlock_irqrestore(&bm.list_lock, flags);

	list_for_each_entry(s, &priv_list, list) {
		if (have_prev) {
			s64 gap_ms = div_s64(s->ns - prev_ns, NSEC_PER_MSEC);

			if (gap_ms < min_gap)
				min_gap = gap_ms;
			if (gap_ms > max_gap)
				max_gap = gap_ms;
			sum_gap += gap_ms;
			n++;
		}
		prev_ns = s->ns;
		have_prev = true;
	}

	mutex_lock(&bm.stats.lock);
	bm.stats.n = n;
	if (n > 0) {
		bm.stats.min_gap_ms = min_gap;
		bm.stats.max_gap_ms = max_gap;
		bm.stats.avg_gap_ms = div_s64(sum_gap, (s64)n);
		bm.stats.valid = true;
	}
	mutex_unlock(&bm.stats.lock);

	list_for_each_entry_safe(s, tmp, &priv_list, list) {
		bm.stats.total_samples++;
		list_del(&s->list);
		kfree(s);
	}

	if (n > 0)
		pr_info("%s: window samples=%llu min=%lldms max=%lldms avg=%lldms\n",
			DRV_NAME, n, min_gap, max_gap, bm.stats.avg_gap_ms);
	else
		pr_info("%s: window had no new sample pairs\n", DRV_NAME);

	/* Re-queue for the next window (skip if we're unloading). */
	schedule_delayed_work(&bm.stats_work, msecs_to_jiffies(STATS_PERIOD_MS));
}

/*
 * -------------------------------------------------------------------
 * /dev/bitsmon character device (misc device, dynamic minor)
 * -------------------------------------------------------------------
 */
static ssize_t bitsmon_dev_read(struct file *filp, char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	char buf[STATS_BUF_SZ];
	int len;

	if (*ppos > 0)
		return 0; /* single-shot read, like a normal /proc file */

	mutex_lock(&bm.stats.lock);
	if (!bm.stats.valid)
		len = scnprintf(buf, sizeof(buf),
				"n=0 min=0 max=0 avg=0 total=%llu\n",
				bm.stats.total_samples);
	else
		len = scnprintf(buf, sizeof(buf),
				"n=%llu min=%lld max=%lld avg=%lld total=%llu\n",
				bm.stats.n, bm.stats.min_gap_ms,
				bm.stats.max_gap_ms, bm.stats.avg_gap_ms,
				bm.stats.total_samples);
	mutex_unlock(&bm.stats.lock);

	if (copy_to_user(ubuf, buf, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

static const struct file_operations bitsmon_fops = {
	.owner	= THIS_MODULE,
	.read	= bitsmon_dev_read,
};

/*
 * -------------------------------------------------------------------
 * sysfs attribute: interval_ms (rw, clamped 100-5000)
 * -------------------------------------------------------------------
 */
static ssize_t interval_ms_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", atomic_read(&bm.interval_ms));
}

static ssize_t interval_ms_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	val = clamp_val(val, INTERVAL_MIN_MS, INTERVAL_MAX_MS);
	atomic_set(&bm.interval_ms, val);

	return count;
}

static DEVICE_ATTR_RW(interval_ms);

static struct attribute *bitsmon_attrs[] = {
	&dev_attr_interval_ms.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bitsmon);

/*
 * -------------------------------------------------------------------
 * Module init / exit
 * -------------------------------------------------------------------
 */
static int __init bitsmon_init(void)
{
	int ret;

	spin_lock_init(&bm.list_lock);
	INIT_LIST_HEAD(&bm.sample_list);
	bm.seq = 0;

	memset(&bm.stats, 0, sizeof(bm.stats));
	mutex_init(&bm.stats.lock);

	atomic_set(&bm.interval_ms, INTERVAL_DEFAULT_MS);

	bm.miscdev.minor = MISC_DYNAMIC_MINOR;
	bm.miscdev.name  = DRV_NAME;
	bm.miscdev.fops  = &bitsmon_fops;
	bm.miscdev.groups = bitsmon_groups;

	ret = misc_register(&bm.miscdev);
	if (ret) {
		pr_err("%s: misc_register failed: %d\n", DRV_NAME, ret);
		return ret;
	}

	INIT_DELAYED_WORK(&bm.stats_work, bitsmon_work_fn);
	schedule_delayed_work(&bm.stats_work, msecs_to_jiffies(STATS_PERIOD_MS));

	bm.thread = kthread_run(bitsmon_thread_fn, NULL, "bitsmon-worker");
	if (IS_ERR(bm.thread)) {
		ret = PTR_ERR(bm.thread);
		pr_err("%s: kthread_run failed: %d\n", DRV_NAME, ret);
		cancel_delayed_work_sync(&bm.stats_work);
		misc_deregister(&bm.miscdev);
		return ret;
	}

	pr_info("%s: module loaded, /dev/%s ready\n", DRV_NAME, DRV_NAME);
	return 0;
}

static void __exit bitsmon_exit(void)
{
	struct bitsmon_sample *s, *tmp;
	unsigned long flags;

	kthread_stop(bm.thread);
	cancel_delayed_work_sync(&bm.stats_work);

	/* Free any records left un-drained at unload time. */
	spin_lock_irqsave(&bm.list_lock, flags);
	list_for_each_entry_safe(s, tmp, &bm.sample_list, list) {
		list_del(&s->list);
		kfree(s);
	}
	spin_unlock_irqrestore(&bm.list_lock, flags);

	misc_deregister(&bm.miscdev);

	pr_info("%s: module unloaded\n", DRV_NAME);
}

module_init(bitsmon_init);
module_exit(bitsmon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>");
MODULE_DESCRIPTION("Kernel-thread sampler with workqueue-based deferred statistics (bitsmon)");
