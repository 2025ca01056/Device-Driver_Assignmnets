/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BITSCORE_H
#define BITSCORE_H

void bitscore_add_sample(int val);
int bitscore_sample_count(void);
int bitscore_sample_avg(void);
int bitscore_get_sum(const int *arr, int count);

#endif /* BITSCORE_H */
