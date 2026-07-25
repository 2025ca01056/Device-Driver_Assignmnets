#!/bin/bash
# test.sh - demonstration/evidence script for bitsmem (Assignment 2, Q2)
# Run as: sudo ./test.sh   (from ch11-bitsmem/ after `make`)

set -e

MODULE=bitsmem
DEV=/dev/bitsmem

echo "=== [1] Loading module ==="
insmod ./${MODULE}.ko
sleep 1

echo "=== [2] Cache visible in /proc/slabinfo ==="
grep bits_rec /proc/slabinfo || echo "WARNING: cache not found"

echo "=== [3] alloc 1000 ==="
echo "alloc 1000" | tee ${DEV}
cat ${DEV}

echo "=== [4] free 400 ==="
echo "free 400" | tee ${DEV}
cat ${DEV}

echo "=== [5] bench ==="
echo "bench" | tee ${DEV}
cat ${DEV}

echo "=== [6] Malformed command -> expect -EINVAL ==="
echo "alloc oops" | tee ${DEV} || echo "(expected failure above: -EINVAL)"

echo "=== [7] Unload with 600 records still outstanding ==="
rmmod ${MODULE}
dmesg | tail -n 5

echo "=== [8] kmemleak scan ==="
if [ -f /sys/kernel/debug/kmemleak ]; then
	echo scan | tee /sys/kernel/debug/kmemleak
	sleep 2
	cat /sys/kernel/debug/kmemleak
else
	echo "kmemleak not enabled on this kernel; skip or enable CONFIG_DEBUG_KMEMLEAK"
fi

echo "=== DONE ==="
