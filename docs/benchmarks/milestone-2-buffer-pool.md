# Milestone 2 benchmark record

## Method

The Release benchmark was run on 2026-09-08 with the Visual Studio 2022 Build Tools generator. It allocates eight 4,096-byte pages, then performs 10,000 reads cycling through those same eight IDs. Direct access invokes `PageManager::read_page` for every operation. Buffered access invokes `fetch_page` then `unpin_page` for every operation with an eight-frame pool. The access cycle is deterministic.

This is a cache-effect benchmark, not a durable-device benchmark. Both variants use the operating-system file cache; no `fsync`/`FlushFileBuffers` is issued. Results are a single local run and should not be compared across machines as a storage-device benchmark.

## Measured results

| Variant | Operations | Total time | Operations/sec | Average latency | Disk reads | Disk writes | Cache hit rate |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Direct `PageManager` reads | 10,000 | 0.017 s | 572,954.267 | 1.745 µs | 10,000 | 0 | N/A |
| `BufferPoolManager` reads | 10,000 | 0.001 s | 11,568,718.186 | 0.086 µs | 8 | 0 | 99.92% |

The first access to each of the eight logical pages misses; the remaining 9,992 accesses hit. The buffer pool therefore avoids 9,992 calls to the underlying page manager for this workload.
