# Milestone 4 query-execution benchmark

## Environment and methodology

Measured locally on 2026-09-09 with the Release configuration, MSVC 14.44, a 64-page buffer pool, and `steady_clock`. The benchmark creates a new temporary database in each process, inserts 10,000 tuples with a 19-byte serialized `(int64, string)` layout, then performs one warm-ish in-process run of each operation. The nested-loop join uses two 1,000-row relations (one million candidate pairs) and equality on the integer column. No cold-cache numbers were collected.

These local measurements are observations, not performance guarantees.

## Results

| Operation | Input / output | Elapsed | Throughput |
| --- | ---: | ---: | ---: |
| Sequential scan | 10,000 / 10,000 tuples | 0.002491 s | 4,014,129.74 tuples/s |
| Filter (`id >= 5000`) | 10,000 / 5,000 tuples | 0.001596 s | 3,132,243.31 tuples/s |
| Seq scan + equality filter | 10,000 / 1 tuple | 0.001514 s | 660.46 output tuples/s |
| Index equality lookup | 1 key / 1 tuple | 0.000049 s | 20,202.02 output tuples/s |
| In-memory sort (`id DESC`) | 10,000 / 10,000 tuples | 0.004179 s | 2,393,031.49 tuples/s |
| Nested-loop equality join | 1,000 x 1,000 / 1,000 tuples | 0.002201 s | 454,235.75 output tuples/s |

For singleton lookup, elapsed latency is the meaningful comparison: 1.514 ms for scan-plus-filter versus 0.049 ms for index lookup in this run.
