# Milestone 5 lock-manager benchmark

Measured locally on 2026-09-09 using MSVC 14.44 Release (`/O2`), `steady_clock`, 100,000 operations, page-level resource IDs, and an in-process `LockManager`. Concurrent runs use four threads. WAIT-DIE may abort younger contenders in the exclusive workload; the benchmark counts completed attempts, not successful grants only.

| Operation | Elapsed | Throughput |
| --- | ---: | ---: |
| Uncontended shared lock/unlock | 0.0227774 s | 4,390,320 ops/s |
| Uncontended exclusive lock/unlock | 0.0226284 s | 4,419,230 ops/s |
| Concurrent shared locking | 0.0220465 s | 4,535,870 ops/s |
| Contended exclusive locking | 0.0318073 s | 3,143,930 ops/s |

These are local measurements, not universal performance guarantees. They do not include disk I/O or a buffer-pool page fetch.
