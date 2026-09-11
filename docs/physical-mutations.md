# Physical mutation infrastructure

`PhysicalMutation` owns payload-only before/after images (4,064 bytes), a stable `PageId`, kind, and sequence number. Headers—including PageLSN—are intentionally preserved by replay. `MutationContext::watch(page)` snapshots the page through the buffer pool; `finish(page)` snapshots again and appends a mutation only when bytes differ. Each watch/finish pair is a separate ordered mutation, including repeated writes to the same page.

`apply_after_image` and `apply_before_image` fetch, overwrite only payload bytes, dirty, and unpin through `BufferPoolManager`.

## WAL integration boundary

For transactional writes, `MutationContext::finish(page_id, pinned_page)` now
captures the physical image while the writer still owns its page pin. A future
WAL finalizer may persist that mutation and return an LSN; the context assigns
that LSN to the page before it is unpinned dirty. When capture transfers a
mutation to a transaction instead, the buffer pool blocks its dirty page from
flush and eviction until the future coordinator finalizes that mutation in
capture order. `Transaction` owns the ordered captured images without copying
them again, but this foundation does not write WAL records or perform recovery.

## TableHeap integration

`TableHeap::create`, `insert`, and `erase` accept an optional `MutationContext`. Insert and erase install it as a scoped active context and restore any prior context with RAII. The context is consulted immediately before and after every physical payload write, in the order the existing algorithm performs those writes:

1. table metadata-page initialization;
2. new data-page initialization;
3. previous data-page `next` link update;
4. table first/last-page metadata update;
5. tuple bytes, slot-directory entry, and free-space fields on the target data page; and
6. the slot live/delete flag.

No-op operations do not emit a mutation because `finish` compares images. Capture is payload-only, so the 32-byte persistent page header and its PageLSN remain untouched. Replay tests reverse before-images and then apply after-images through the buffer pool, verifying exact payload restoration and normal pin-count cleanup.

B+ tree capture is integrated independently using the same context. WAL, REDO, UNDO, and crash recovery are not implemented.
