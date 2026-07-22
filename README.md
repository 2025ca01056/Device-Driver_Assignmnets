# Assignment 1 — Kernel Build System, Module Stacking & Emulated MMIO

**Student Name:** Dhruv Patel  
**Student ID:** 2025ca01056  
**Email:** 2025ca01056@wilp.bits-pilani.ac.in  
**Platform:** Ubuntu 24.04 LTS (x86_64 Virtual Machine running Linux Kernel 6.x)  
**Repository:** https://github.com/2025ca01056/Device-Driver_Assignmnets

---

## Directory Layout

```text
bits-ddrv-2025ca01056/
├── ch04-stack/
│   ├── core.c          # Core module maintaining sample store and exports
│   ├── stats.c         # Statistical processing engine helper
│   ├── bitsfeed.c      # Consumer module driving exported APIs via jiffies
│   ├── Makefile        # Out-of-tree build script with clean & install targets
│   └── test.sh         # Test script for dependencies, refcount, and modprobe
├── ch06-bits7seg/
│   ├── bits7seg.c      # Multi-minor character driver with emulated registers
│   ├── Makefile        # Out-of-tree build script
│   └── test.sh         # Verification script for 4 minors, sysfs, and -EINVAL
└── README.md
