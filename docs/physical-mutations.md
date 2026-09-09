# Physical mutation infrastructure

`PhysicalMutation` owns payload-only before/after images (4,064 bytes), a stable `PageId`, kind, and sequence number. Headers—including PageLSN—are intentionally preserved by replay. `MutationContext::watch(page)` snapshots the page through the buffer pool; `finish(page)` snapshots again and appends a mutation only when bytes differ. Each watch/finish pair is a separate ordered mutation, including repeated writes to the same page.

`apply_after_image` and `apply_before_image` fetch, overwrite only payload bytes, dirty, and unpin through `BufferPoolManager`.

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
