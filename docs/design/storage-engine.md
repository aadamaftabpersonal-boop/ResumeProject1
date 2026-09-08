# Milestone 1: Fixed-page storage engine

## Purpose and architecture

This layer owns persistent, fixed-size pages. `PageManager` owns one database file and is the only component that reads, writes, allocates, or flushes it. A `Page` owns its 4,096-byte in-memory payload and associated `PageId`.

```text
PageManager -> database file -> disk
```

There is intentionally no buffer cache, metadata page, index, query layer, concurrency control, transaction, or recovery system in this milestone.

## Page format and file layout

Each page payload is exactly 4,096 bytes. The file is a contiguous sequence with no header:

```text
offset 0       Page 0 (4096 bytes)
offset 4096    Page 1 (4096 bytes)
offset N*4096  Page N (4096 bytes)
```

Page `N` starts at `N * 4096`. `PageId` has an explicit invalid sentinel; invalid, unallocated, or unrepresentable IDs are rejected with `StorageError`.

## Allocation and reopening

Allocation is sequential. To allocate an ID, the manager appends one zero-filled page and returns the prior page count. On open, it obtains the file length and requires it to be an exact multiple of 4,096. The next ID is `file_length / 4096`. Thus file length itself is the persistent allocation record; there is no in-memory-only allocation state or separate metadata format.

## Persistence model

`write_page` writes to the C++ file stream; `flush` flushes that stream to the operating-system file layer. Destructor calls `flush` as best effort and cannot report a failure. This does not provide durable power-loss guarantees: it does not issue an OS `fsync`/`FlushFileBuffers`, and there is no WAL. Callers that need error reporting must call `flush` explicitly.

## Error handling and invariants

Recoverable storage failures throw `StorageError`: creation/open failures, bad file inspection, malformed file length, invalid/out-of-range IDs, seek failure, short reads, write failure, and flush failure. No operation silently converts an I/O failure into success.

Key invariants are: page payload size is 4,096 bytes; file length is page-aligned; every allocated page occupies exactly one page; and a page written through `write_page(id, page)` has the same ID as `id`.

## Tradeoffs

Using file size as the allocation record is deterministic, inspectable, and avoids prematurely reserving a metadata page. Appending a zero page makes allocation visible in the file immediately. The tradeoff is that allocation has no reuse/free-list, concurrent access is unsupported, and a crash between underlying OS operations can still require later recovery work. Those are explicitly deferred milestones.
