# WAL/transaction integration architecture

This document establishes the boundaries required by the forthcoming local WAL
and recovery milestone. It intentionally does **not** implement a WAL file,
records, logging, recovery, redo, undo, checkpoints, or crash injection.

## WAL-before-data

`BufferPoolManager` accepts an optional `WalDurabilityProvider`. Before every
dirty-frame write (explicit flush, flush-all, eviction, delete-page writeback,
and destructor flushing), it verifies that either the page PageLSN is zero or
the provider's durable LSN is at least the page PageLSN. A nonzero PageLSN
without a provider, or ahead of the provider, raises `WalDurabilityError`; the
frame remains dirty and is not written. Destructors preserve the existing
no-throw rule and therefore suppress the error while leaving the data unwritten.

## PageLSN lifecycle

The persistent PageLSN remains in bytes 16..23 of the established 32-byte page
header. `MutationContext::finish(page_id, pinned_page)` captures the after image
while the writer still pins the page, then delivers it to a `MutationFinalizer`.
An immediately finalized mutation returns a nonzero LSN, which is assigned
before the caller can unpin dirty. A transaction-owned captured mutation has no
LSN yet: the buffer pool counts it as pending and refuses every write-back and
eviction path, even while its PageLSN is still zero. The future coordinator
calls `Transaction::finalize_next_mutation(lsn)` in capture order after writing
each real WAL record; that sets PageLSN and releases the matching pending gate.
This prerequisite never allocates or invents LSNs.

## Undo PageLSN resolution

Normal `finalize_next_mutation(lsn)` remains a forward-only WAL path and rejects
zero. Undo uses the separate reverse-only transaction resolution path after it
has restored a before-image. A still-pending capture uses
`resolve_pending_mutation(page, restored_lsn)` to release its flush gate; a
mutation already finalized by WAL uses `restore_page_lsn_after_undo`. Both
permit zero. The restored LSN is the most recent surviving mutation for the
same page, never `mutation_lsn - 1`; if none survives, it is zero.

## Transaction ownership and coordination

`Transaction` is a `MutationFinalizer`. Its lazily-created `MutationContext`
is tied to one buffer pool and appends ordered `PhysicalMutation` values directly
to the transaction's pending-mutation vector, without duplicating images. It
records a separate finalized prefix rather than copying or rewriting mutation
images. Transactional TableHeap and B+ tree wrappers pass that context to every
mutation API; non-transactional APIs remain unchanged.

`TransactionCoordinator`, rather than `LockManager`, owns future durability
ordering. Its optional `TransactionDurabilityParticipant` seam runs before
`LockManager` transitions the transaction state and releases locks. A future
participant will enforce commit's mutation-log/commit-record/durable-WAL order
and abort's reverse physical undo/abort-record order. The coordinator rejects a
terminal transition while captured mutations remain unresolved. The current
participant API does not log or undo anything.

## Database open lifecycle

`Database` is the small lifecycle owner. An optional `DatabaseLifecycle` is
called in this order: `open_wal(database + ".wal")`, open the data
`PageManager`, `recover(page_manager)`, then create `BufferPoolManager` and
transaction services. These callbacks are no-ops by default; their purpose is
to ensure future recovery completes before normal clients receive services.

## Intentionally absent

No persistent WAL, record format, checksums, LSN allocator, commit durability,
abort undo, redo, recovery scan, checkpoint, or crash-injection facility exists
in this prerequisite.
