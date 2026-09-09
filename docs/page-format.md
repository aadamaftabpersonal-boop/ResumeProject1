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
