# Milestone 3 B+ tree benchmark

## Environment and method

Run on 2026-09-08 using Visual Studio 2022 Build Tools / MSVC 19.44 in Release mode. The benchmark creates a fresh temporary database, uses a 256-frame buffer pool, 4096-byte pages, and `std::mt19937_64` seed `123456789`.

It inserts 10,000 unique shuffled `int64_t` keys, then performs 10,000 shuffled point lookups (80% existing, 20% missing), and 100 deterministic range scans of 100 keys. Timed sections exclude key generation and tree creation. A checksum consumes lookup and scan results. The baseline performs the same lookup workload over a contiguous linear vector.

The OS page cache and in-process buffer pool affect these numbers. This is one local run, not a universal storage or algorithm comparison.

## Results

| Workload | Operations | Time | Throughput | Average latency |
| --- | ---: | ---: | ---: | ---: |
| Insert | 10,000 | 0.067825 s | 147,438.912 ops/s | 6.782 µs |
| B+ tree lookup | 10,000 | 0.048204 s | 207,453.816 ops/s | 4.820 µs |
| Linear baseline lookup | 10,000 | 0.005828 s | 1,715,883.938 ops/s | 0.583 µs |
| Range scan | 100 scans / 10,000 rows | 0.003792 s | 26,369.917 scans/s | 37.922 µs/scan |

The populated tree had height 4 and 966 allocated pages. Point lookups produced 8,000 hits and 2,000 misses; range scans returned 10,000 rows. The output checksum was `176854492`.

## Interpretation and limitations

The tree performs genuine logarithmic page navigation and ordered leaf traversal; the contiguous in-memory linear baseline is faster at this modest dataset because it avoids page decoding, pin/unpin bookkeeping, and pointer-like navigation. The result does not invalidate the B+ tree’s asymptotic benefit at larger data sizes or when the alternative requires scanning persistent records. It does not measure durable-media latency, concurrency, WAL/recovery overhead, or any distributed behavior.
