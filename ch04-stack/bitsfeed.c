// SPDX-License-Identifier: GPL-2.0
/*
 * Consumer module feeding sample data.
 * Author: Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>
 */

#define pr_fmt(fmt) "bitsfeed: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/ratelimit.h>
#include "bitscore.h"

static int nsamples = 5;
module_param(nsamples, int, 0644);
MODULE_PARM_DESC(nsamples, "Number of samples to generate (1-50)");

static int __init bitsfeed_init(void)
{
	int i;
	int clamped_nsamples = nsamples;

	if (nsamples < 1 || nsamples > 50) {
		pr_warn_ratelimited("nsamples %d out of bounds (1-50); clamping\n",
				    nsamples);
		if (nsamples < 1)
			clamped_nsamples = 1;
		else
			clamped_nsamples = 50;
	}

	pr_info("Feeding %d samples using jiffies\n", clamped_nsamples);

	for (i = 0; i < clamped_nsamples; i++) {
		int sample = (int)(jiffies & 0xFF) + i;

		bitscore_add_sample(sample);
	}

	pr_info("Sample Count: %d, Average: %d\n",
		bitscore_sample_count(), bitscore_sample_avg());

	return 0;
}

static void __exit bitsfeed_exit(void)
{
	pr_info("Consumer module unloaded\n");
}

module_init(bitsfeed_init);
module_exit(bitsfeed_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>");
MODULE_DESCRIPTION("Consumer module driving bitscore API");
