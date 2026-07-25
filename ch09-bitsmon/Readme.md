# bitsmon — Kernel-Thread Sampler with Deferred Statistics

**Author:** Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>
**BITS ID:** 2025CA01056
**Assignment:** 2, Question 1 

## What it does

- On `insmod`, a kernel thread `bitsmon-worker` (via `kthread_run`) wakes up
  every `interval_ms` (default 1000 ms, configurable 100-5000 ms) and appends
  a `{seq, jiffies, ktime_get_ns()}` record onto a shared list, protected by
  a spinlock.
- A delayed work item (`INIT_DELAYED_WORK`, requeued every 5 s) drains that
  list, computes min/max/average inter-sample gap in milliseconds, stores
  the result under a mutex, prints one `pr_info` summary line, and
  reschedules itself.
- `/dev/bitsmon` (misc device, dynamic minor) - `read()` returns the latest
  stats snapshot as text.
- `/sys/class/misc/bitsmon/interval_ms` - read/write sysfs attribute,
  clamped to [100, 5000] ms, takes effect on the *next* sampler iteration
  (no reload needed).
- `rmmod` calls `kthread_stop()`, then `cancel_delayed_work_sync()`, then
  frees any records still queued - verified leak-free with kmemleak.

## Why the spinlock protects only the list (design justification)

The spinlock (`bm.list_lock`) guards **only** `bm.sample_list` and the
`seq` counter - nothing else. This is deliberate:

1. **Spinlocks must never be held across a sleeping or long-running
   operation.** The stats computation (walking N records, computing
   min/max/average, calling `pr_info`) can take an unbounded amount of
   time proportional to however many samples accumulated in the last 5
   seconds. Holding a spinlock for that long would disable preemption
   (and on SMP, busy-spin every other CPU trying to acquire it) for the
   entire computation.

2. **`pr_info()` can, in principle, block** (console output under load,
   or occasionally schedule). Calling anything that can sleep while
   holding a spinlock is illegal in Linux and would trigger
   `might_sleep()` / "scheduling while atomic" on a debug kernel.

3. **The producer must not be starved.** `bitsmon_thread_fn()` also needs
   the same lock. If the consumer held the lock throughout the whole
   drain-and-compute pass, the producer thread would stall on every tick
   whenever the workqueue fires.

**The fix used here:** the work handler takes the lock only long enough to
call `list_splice_init()`, which atomically moves every entry from the
shared list onto a private, on-stack `list_head` (`priv_list`) that no
other context can see. The lock is then dropped immediately. All
processing - the walk, min/max/average computation, `pr_info`, and
`kfree()` of each record - happens against `priv_list` with no lock
held at all, because it is now private to this work invocation. This
gives an O(1) critical section regardless of how many samples piled up,
while the expensive part runs fully concurrently with the producer.

The **mutex** (`bm.stats.lock`) protects the *result* structure
(`bm.stats`), which is written once per 5-second window by the workqueue
and read on-demand by any process doing `cat /dev/bitsmon`. A mutex is
appropriate here (rather than another spinlock) because `bitsmon_dev_read()`
runs in process context on behalf of a user syscall and there is no
requirement to avoid sleeping - using a mutex avoids unnecessarily
disabling preemption just to protect a small read/copy of a stats struct.

## Build & test

```bash
make
sudo insmod ./bitsmon.ko
sudo ./test.sh
sudo rmmod bitsmon
```

## Manual demonstration commands

```bash
sudo insmod ./bitsmon.ko
ps -eLo comm | grep bitsmon
dmesg -wH
cat /dev/bitsmon
echo 250 | sudo tee /sys/class/misc/bitsmon/interval_ms
cat /dev/bitsmon
sudo rmmod bitsmon
echo scan | sudo tee /sys/kernel/debug/kmemleak && cat /sys/kernel/debug/kmemleak
```

## Evidence

All Evidence snipshot also submitted as PDF assignment.


