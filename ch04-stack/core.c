// SPDX-License-Identifier: GPL-2.0
/*
 * ch04-stack/core.c - Core module maintaining sample store and exports.
 * Author: Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>
 */

#define pr_fmt(fmt) "bitscore: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#define MAX_SAMPLES 100

static int samples[MAX_SAMPLES];
static int sample_count_val;
static DEFINE_SPINLOCK(sample_lock);

/**
 * bitscore_add_sample - Add a sample value to the sample array
 * @val: Value to add
 */
void bitscore_add_sample(int val)
{
	unsigned long flags;

	spin_lock_irqsave(&sample_lock, flags);
	if (sample_count_val < MAX_SAMPLES) {
		samples[sample_count_val] = val;
		sample_count_val++;
	} else {
		pr_warn("Sample buffer full!\n");
	}
	spin_unlock_irqrestore(&sample_lock, flags);
}
EXPORT_SYMBOL_GPL(bitscore_add_sample);

/**
 * bitscore_sample_count - Get the current count of samples
 *
 * Return: Current sample count
 */
int bitscore_sample_count(void)
{
	unsigned long flags;
	int count;

	spin_lock_irqsave(&sample_lock, flags);
	count = sample_count_val;
	spin_unlock_irqrestore(&sample_lock, flags);

	return count;
}
EXPORT_SYMBOL_GPL(bitscore_sample_count);

/* Helper function defined in stats.c */
extern int bitscore_get_sum(const int *arr, int count);

/**
 * bitscore_sample_avg - Calculate the average of recorded samples
 *
 * Return: Integer average of samples, or 0 if no samples
 */
int bitscore_sample_avg(void)
{
	unsigned long flags;
	int count, sum = 0, avg = 0;

	spin_lock_irqsave(&sample_lock, flags);
	count = sample_count_val;
	if (count > 0) {
		sum = bitscore_get_sum(samples, count);
		avg = sum / count;
	}
	spin_unlock_irqrestore(&sample_lock, flags);

	return avg;
}
EXPORT_SYMBOL_GPL(bitscore_sample_avg);

static int __init bitscore_init(void)
{
	pr_info("Core module loaded successfully\n");
	return 0;
}

static void __exit bitscore_exit(void)
{
	pr_info("Core module unloaded\n");
}

module_init(bitscore_init);
module_exit(bitscore_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>");
MODULE_DESCRIPTION("Core sample storage and processing engine");
