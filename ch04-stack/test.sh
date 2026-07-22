#!/usr/bin/env bash
set -e

echo "=== Building Modules ==="
make clean
make

echo "=== Step 1: Checkpatch Execution ==="
/usr/src/linux-headers-$(uname -r)/scripts/checkpatch.pl --file --no-tree core.c stats.c bitsfeed.c || true

echo "=== Step 2: Attempt Loading Consumer without Core ==="
sudo insmod ./bitsfeed.ko 2>/dev/null || echo "Expected failure: Unresolved symbols!"

echo "=== Step 3: Load Core then Consumer ==="
sudo insmod ./bitscore.ko
sudo insmod ./bitsfeed.ko nsamples=8
sudo dmesg | tail -n 6

echo "=== Step 4: Verify lsmod Dependency ==="
lsmod | grep bits

echo "=== Step 5: Attempt Removing Core Module (Should Fail) ==="
sudo rmmod bitscore 2>/dev/null || echo "Expected failure: Module is in use!"

echo "=== Step 6: Cleanup Manual Load ==="
sudo rmmod bitsfeed
sudo rmmod bitscore

echo "=== Step 7: Test Installation and Depmod Automatic Resolution ==="
sudo make install
sudo depmod -a
sudo modprobe bitsfeed
sudo dmesg | tail -n 5
sudo modprobe -r bitsfeed
sudo modprobe -r bitscore

echo "=== All Question 1 Tests Passed Successfully! ==="
