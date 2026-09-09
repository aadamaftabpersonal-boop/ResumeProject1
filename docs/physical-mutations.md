# Physical mutation infrastructure

`PhysicalMutation` owns payload-only before/after images (4,064 bytes), a stable `PageId`, kind, and sequence number. Headers—including PageLSN—are intentionally preserved by replay. `MutationContext::watch(page)` snapshots the page through the buffer pool; `finish(page)` snapshots again and appends a mutation only when bytes differ. Each watch/finish pair is a separate ordered mutation, including repeated writes to the same page.

`apply_after_image` and `apply_before_image` fetch, overwrite only payload bytes, dirty, and unpin through `BufferPoolManager`. This is infrastructure only: B+ tree and TableHeap hooks are not integrated yet. WAL and crash recovery are not implemented.
