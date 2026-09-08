# Distributed Database Engine

A systems-focused database engine being built incrementally from first principles. The current implementation covers persistent fixed-page storage, a fixed-capacity LRU buffer pool, and a persistent B+ tree. It is not a SQL database and has no transactions, recovery, networking, or distributed behavior.

## Current architecture

```text
Higher-level components -> BPlusTree -> BufferPoolManager -> PageManager -> database file -> disk
```

Pages are 4,096 bytes and occupy contiguous positions in a database file. Page `N` begins at byte `N * 4096`. Sequential allocation is recovered after reopening from the page-aligned file size.

## Current capabilities

- Allocate zero-filled pages sequentially.
- Read, write, and flush 4,096-byte pages.
- Reopen an existing file and continue allocation correctly.
- Reject invalid page IDs, unallocated reads/writes, malformed file lengths, and I/O failures with exceptions.
- Cache a fixed number of pages in memory with page-table lookup, pin counts, dirty tracking, and LRU eviction.
- Write dirty cached pages before eviction and explicitly flush individual or all cached pages.
- Index fixed-width keys with persistent B+ tree insertion, lookup, deletion, range scan, splits, merges, and forward leaf links.

See [the storage design](docs/design/storage-engine.md) and [buffer-pool design](docs/design/buffer-pool.md) for invariants and persistence semantics.
See [the B+ tree design](docs/design/bplus-tree.md) for its on-page layout and structural invariants.
The [B+ tree benchmark record](docs/benchmarks/milestone-3-bplus-tree.md) documents a reproducible local Release run.

## Build and test

Requires CMake 3.20+ and a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

For GCC or Clang sanitizer coverage:

```sh
cmake -S . -B build-sanitized -DDDB_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

## Benchmark

The benchmark compares 10,000 repeated direct `PageManager` reads with equivalent `BufferPoolManager` fetch/unpin operations. It reports operation count, elapsed time, operations/second, average latency, cache hit rate, and disk-operation counters. See the [Milestone 2 benchmark record](docs/benchmarks/milestone-2-buffer-pool.md) for an actual local Release run and its methodology.

```sh
cmake --build build --target storage_benchmark --config Release
./build/storage_benchmark
```

## Roadmap

1. Fixed-page storage engine (complete implementation; verification pending a local toolchain)
2. Buffer pool with LRU replacement (current; verification pending a local toolchain)
3. B+ tree index (current)
4. Query execution
5. Transactions and concurrency
6. Write-ahead logging and recovery
7. Distributed storage, replication, and sharding

## Current limitations

Single-process synchronous file I/O only. There is no page reuse, checksumming, OS-level durable sync, locking, transaction support, WAL, SQL, or distributed functionality.
