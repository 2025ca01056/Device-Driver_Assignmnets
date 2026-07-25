# bitsmem — Slab-Cache Playground & Allocator Benchmark

**Author:** Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>
**BITS ID:** 2025CA01056
**Assignment:** 2, Question 2

## What it does

- On load, creates a private slab cache `bits_rec` via `kmem_cache_create()`
  for a 64-byte record; visible in `/proc/slabinfo`. Destroyed at unload.
- `/dev/bitsmem` accepts text commands via `write()`:
  - `alloc N` - allocate N records from the cache, add to a tracked list.
  - `free N` - free N records from the tracked list.
  - `bench` - run the kmalloc-vs-vmalloc benchmark.
  - Malformed commands return `-EINVAL`. Allocation failure mid-batch
    returns `-ENOMEM` after rolling back the partial batch.
- `read()` returns: records currently outstanding, high-water mark, cache
  object size, and the most recent benchmark results.
- Benchmark: for 64 KiB and 1 MiB, measures `ktime_get()`-timed
  alloc+free cost of `kmalloc(GFP_KERNEL)` vs `vmalloc()`, averaged over
  100 iterations each, all four timings stored in nanoseconds.
- Unload frees every outstanding record before `kmem_cache_destroy()`.

## Allocator analysis (assessed)

### Why kmalloc memory is physically contiguous and vmalloc memory is not

`kmalloc()` allocates from the slab/slub allocator, which carves memory
out of the buddy allocator's physically contiguous page blocks. A
`kmalloc()` allocation lives in one contiguous run of physical pages.
This is fast but limited by fragmentation and `MAX_ORDER`.

`vmalloc()` allocates a virtually contiguous range from a dedicated
kernel virtual address region, but backs each page individually from
wherever the page allocator can find one. The MMU/page tables stitch
scattered physical pages into one contiguous virtual range. So vmalloc
memory is virtually contiguous but usually physically scattered.

**Implication for DMA:** most DMA-capable devices only understand
physical addresses and expect a contiguous physical region per
descriptor. Since vmalloc memory is not physically contiguous, it is
generally unsafe to hand directly to a DMA engine as a single buffer.
Buffers meant for DMA should come from `kmalloc()` / `dma_alloc_coherent()`
instead, or the driver must use scatter-gather DMA (`dma_map_sg`).

### When kvmalloc() is the right call

`kvmalloc()` tries `kmalloc()` first and transparently falls back to
`vmalloc()` if the size is too large or the buddy allocator can't
satisfy it due to fragmentation. It is the right call when the
allocation size is variable or potentially large, physical contiguity
is not required (no DMA), and the caller is not in atomic/interrupt
context (since the vmalloc fallback can sleep).

### A scenario in this driver where GFP_ATOMIC would be required

If `bitsmem_alloc_n()` were called from a context that cannot sleep -
for example an interrupt handler, tasklet, or while holding a spinlock
(unlike the current design, which runs in process context under a
mutex and safely uses `GFP_KERNEL`) - then
`kmem_cache_alloc(bmem.cache, GFP_ATOMIC)` would be required instead.
`GFP_KERNEL` may sleep to reclaim memory; `GFP_ATOMIC` never sleeps and
dips into an emergency reserve instead, trading a higher chance of
failure for a guarantee it will never block the caller.

## Build & test

```bash
make
sudo insmod ./bitsmem.ko
sudo ./test.sh
sudo rmmod bitsmem
```

## Manual demonstration commands

```bash
sudo insmod ./bitsmem.ko
grep bits_rec /proc/slabinfo
echo "alloc 1000" | sudo tee /dev/bitsmem
sudo cat /dev/bitsmem
echo "free 400" | sudo tee /dev/bitsmem
echo "bench" | sudo tee /dev/bitsmem && sudo cat /dev/bitsmem
echo "alloc oops" | sudo tee /dev/bitsmem
sudo rmmod bitsmem
echo scan | sudo tee /sys/kernel/debug/kmemleak && cat /sys/kernel/debug/kmemleak
```

## Evidence
This are part of Assignment PDF I shared.
