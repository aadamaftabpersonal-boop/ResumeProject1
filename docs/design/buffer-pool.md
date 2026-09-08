# Milestone 2: Buffer pool manager

## Architecture

```text
Higher-level components -> BufferPoolManager -> PageManager -> database file -> disk
                              |
                         fixed in-memory frames
```

`BufferPoolManager` borrows a `PageManager`; it never owns or closes the database file. It owns exactly the fixed number of `Frame` objects requested during construction. It is deliberately single-threaded: synchronization, transactions, and recovery are outside this milestone.

## Frame and page table

Each frame contains a `Page`, its unsigned pin count, dirty flag, and occupancy flag. An `unordered_map<PageId, FrameId>` is the page table. An occupied frame is inserted into the table exactly once, and every table entry must reference an occupied frame holding its key. `validate_invariants()` exposes an internal consistency check for tests.

## Pinning and LRU

`fetch_page` returns a pinned `Page*`. A hit increments its pin count. A miss obtains a free frame or the least-recently-used unpinned frame; if none exists, it returns `nullptr` without evicting a pinned page. `unpin_page` decrements a positive count and ORs in its dirty argument. When a count reaches zero, its frame is placed at the MRU end of the LRU list.

The LRU list contains only unpinned frames. A map from `FrameId` to list iterator makes insertion/removal O(1) average, while the front is the O(1) victim. Re-pinning removes a frame from LRU; final unpin restores it at MRU, which records its most recent completed use.

## Dirty pages and flushing

Dirty data is written through `PageManager` before eviction. `flush_page` writes a cached page, flushes the underlying manager, then marks it clean. `flush_all_pages` writes all dirty frames, flushes once, and marks them clean only after that flush succeeds. A failed write leaves an eviction candidate in the LRU/page table, preserving consistency for retry.

The destructor makes a best-effort `flush_all_pages`; callers that need I/O failures reported must call it explicitly. As in Milestone 1, this reaches the file-stream/OS layer only and is not a power-loss durability guarantee.

## Delete behavior

`delete_page` removes only the cached frame; it does not reclaim disk space or make the logical page ID reusable. Pinned pages cannot be deleted. Deleting a non-cached page returns true because its cache-removal postcondition is already satisfied.

## Complexity

| Operation | Expected complexity |
| --- | --- |
| Page-table lookup/insert/remove | O(1) average |
| Pin/unpin and LRU update | O(1) average |
| Victim selection | O(1) |
| Flush/eviction I/O | Filesystem-dependent |

## Failure behavior and invariants

Invalid page IDs and lower-level storage errors retain `PageManager`'s `StorageError` behavior. Fetch returns `nullptr` for an invalid ID, a full all-pinned pool, or pin-count overflow; storage read failures are propagated. The implementation preserves: no duplicate cached PageIds; non-negative pin counts; pinned frames absent from LRU; unpinned occupied frames present in LRU; and dirty victims written before their mapping is removed.
