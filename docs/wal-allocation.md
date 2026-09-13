# Transactional page allocation

Transactional B+ Tree node allocation (leaf splits, internal splits, and new
roots) and TableHeap data-page growth now route through the transaction-owned
allocator. The transaction delegates to `PageManager::allocate_transactional_page`,
which reserves physical space, appends `PAGE_ALLOCATE` with that transaction ID,
durably flushes the WAL, and only then activates the logical allocation bit.

Consequently, a new page is fetchable before `MutationContext` begins capturing
its initialization and later structural mutations. Its `PAGE_ALLOCATE` record
always precedes its first `PHYSICAL_MUTATION`; both use the same transaction ID.
Existing node, page-link, and metadata writes keep their normal ordered
before-image/after-image capture and PageLSN handling. Allocation/flush failure
propagates to the operation: the physical slot remains, but it is logically free
and ordinary buffer-pool fetch rejects it.

The non-transactional B+ Tree and TableHeap APIs retain their original direct
allocation behavior. A transaction without a WAL sink likewise preserves the
pre-existing in-memory mutation behavior; a transaction with a WAL sink uses the
durable allocation path above.

REDO, UNDO, and crash recovery are **not implemented yet**. PAGE_FREE,
checkpoints, replication, sharding, and distributed query support are also out
of scope.
