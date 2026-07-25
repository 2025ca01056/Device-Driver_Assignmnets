#!/bin/bash
set -e
MODULE=bitsmon
DEV=/dev/bitsmon

echo "=== [1] Loading module ==="
insmod ./${MODULE}.ko
sleep 1

echo "=== [2] Kernel thread visible in ps ==="
ps -eLo pid,comm | grep ${MODULE}-worker || echo "WARNING: thread not found"

echo "=== [3] Waiting 6s for first delayed-work stats window ==="
sleep 6
dmesg | tail -n 5

echo "=== [4] Reading /dev/bitsmon snapshot ==="
cat ${DEV}

echo "=== [5] Changing interval_ms to 250 via sysfs ==="
SYSFS_PATH=$(find /sys -name "interval_ms" 2>/dev/null | head -n1)
echo "Found sysfs attribute at: $SYSFS_PATH"
if [ -z "$SYSFS_PATH" ]; then
	echo "ERROR: interval_ms sysfs attribute not found"
	exit 1
fi
echo 250 | tee "$SYSFS_PATH"
cat "$SYSFS_PATH"

echo "=== [6] Waiting 6s more, expect avg gap near ~250ms ==="
sleep 6
cat ${DEV}

echo "=== [7] Unloading module ==="
rmmod ${MODULE}
dmesg | tail -n 5

echo "=== [8] kmemleak scan ==="
if [ -f /sys/kernel/debug/kmemleak ]; then
	echo scan | tee /sys/kernel/debug/kmemleak
	sleep 2
	cat /sys/kernel/debug/kmemleak
else
	echo "kmemleak not enabled on this kernel"
fi
echo "===2025ca01056 DDrv Lab1 Q1 successfully DONE ==="
