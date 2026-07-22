#!/usr/bin/env bash
set -e

echo "=== Building and Inserting Module ==="
make clean
make
sudo insmod ./bits7seg.ko

echo "=== Verifying Device Nodes ==="
ls -l /dev/bits7seg*
grep bits7seg /proc/devices

echo "=== Writing Digits to Minors ==="
echo -n "1" | sudo tee /dev/bits7seg0 > /dev/null
echo -n "4" | sudo tee /dev/bits7seg1 > /dev/null
echo -n "7" | sudo tee /dev/bits7seg2 > /dev/null
echo -n "9" | sudo tee /dev/bits7seg3 > /dev/null

echo "=== Reading Back Character Device State ==="
for i in {0..3}; do
    echo -n "/dev/bits7seg$i: "
    sudo cat /dev/bits7seg$i
done

echo "=== Sysfs Read and Modification Test ==="
echo "Current sysfs digit on dev2:"
cat /sys/class/bits7seg/bits7seg2/digit
echo "3" | sudo tee /sys/class/bits7seg/bits7seg2/digit > /dev/null
echo -n "Updated dev2 char state: "
sudo cat /dev/bits7seg2

echo "=== Testing Invalid Input Error Handling (-EINVAL) ==="
if echo -n "x" | sudo tee /dev/bits7seg0 2>/dev/null; then
    echo "ERROR: Invalid write succeeded unexpectedly!"
    exit 1
else
    echo "SUCCESS: Invalid write correctly rejected with -EINVAL"
fi

echo "=== Unloading Module ==="
sudo rmmod bits7seg
ls -l /dev/bits7seg* 2>/dev/null || echo "SUCCESS: Device nodes removed cleanly!"

echo "=== All lab1 Question 2 Tests Passed Successfully! ==="
