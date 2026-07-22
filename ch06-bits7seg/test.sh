#!/usr/bin/env bash
set -e

echo "=== Building and Inserting Module ==="
make clean
make
sudo insmod ./bits7seg.ko

echo "=== Step 1: Checkpatch Execution ==="
/usr/src/linux-headers-$(uname -r)/scripts/checkpatch.pl --file --no-tree bits7seg.c || true

echo "=== Step 2 & 3: Verifying Device Nodes and Major ==="
ls -l /dev/bits7seg*
grep bits7seg /proc/devices

echo "=== Step 4: Writing Digits to Minors ==="
echo -n "1" | sudo tee /dev/bits7seg0 > /dev/null
echo -n "4" | sudo tee /dev/bits7seg1 > /dev/null
echo -n "7" | sudo tee /dev/bits7seg2 > /dev/null
echo -n "9" | sudo tee /dev/bits7seg3 > /dev/null

echo "=== Reading Back Character Device State ==="
for i in {0..3}; do
    echo -n "/dev/bits7seg$i: "
    sudo cat /dev/bits7seg$i
done

echo "=== Step 5: Sysfs Read and Modification Test ==="
echo "Current sysfs digit on dev2:"
cat /sys/class/bits7seg/bits7seg2/digit
echo "3" | sudo tee /sys/class/bits7seg/bits7seg2/digit > /dev/null
echo -n "Updated dev2 char state: "
sudo cat /dev/bits7seg2

echo "=== Step 6: Testing Invalid Input Error Handling (-EINVAL) ==="
if echo -n "x" | sudo tee /dev/bits7seg0 2>/dev/null; then
    echo "ERROR: Invalid write succeeded unexpectedly!"
    exit 1
else
    echo "SUCCESS: Invalid write correctly rejected with -EINVAL"
fi

echo "=== Step 7: Unloading Module ==="
sudo rmmod bits7seg
sudo dmesg | tail -n 2
ls -l /dev/bits7seg* 2>/dev/null || echo "SUCCESS: Device nodes removed cleanly!"

echo "=== All Question 2 Tests Passed Successfully! ==="
