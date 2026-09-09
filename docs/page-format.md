# Versioned page format

Each physical page is exactly 4,096 bytes. Version 1 uses a 32-byte envelope and a 4,064-byte application payload.

```text
0..3    magic "DDBP"
4..5    little-endian format version (1)
6..7    little-endian header size (32)
8..15   little-endian PageId
16..23  little-endian PageLSN (initially zero)
24..31  reserved, zero
32..4095 TableHeap or B+ tree payload
```

`Page::data()` and `Page::size()` refer only to the payload. `PageManager` explicitly serializes and validates the envelope. A mismatched magic, version, header size, or page ID raises `StorageError`; pre-versioned Milestone 1–5 pages are therefore explicitly rejected as unsupported legacy format. WAL and crash recovery are not implemented yet.

Final 6A-1 verification (2026-09-09): all six Debug and all six MSVC AddressSanitizer binaries exited successfully. Direct ASan builds emitted only MSVC's C5072/LNK4302 requests for debug symbols; no sanitizer error was reported. Release regression measurements: direct storage reads 598,942 ops/s; buffered reads 12,254,902 ops/s; B+ tree insertion 153,782 ops/s and lookup 210,247 ops/s (10,000 keys, 256 frames); query sequential scan 5,728,033 tuples/s, sort 3,793,339 tuples/s, and index equality lookup 0.027 ms. The aggregate executor's prior C4244 is unrelated to this format migration: its numeric SUM/AVG visitor returns an `int64_t` through a `double` result path. No warning suppression was added.
