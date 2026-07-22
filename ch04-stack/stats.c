// SPDX-License-Identifier: GPL-2.0
/*
 * Statistical helper for bitscore module.
 * Author: Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>
 */

#include <linux/kernel.h>
#include "bitscore.h"

/**
 * bitscore_get_sum - Helper to compute sum of integer array
 * @arr: Pointer to sample array
 * @count: Number of valid elements
 *
 * Return: Sum of elements
 */
int bitscore_get_sum(const int *arr, int count)
{
	int i, sum = 0;

	for (i = 0; i < count; i++)
		sum += arr[i];

	return sum;
}
