# Transactions and concurrency

Milestone 5 adds in-process transactional coordination and page-level two-phase locking. It does not add logging, WAL, recovery, MVCC, SQL, distributed transactions, or rollback of physical changes.

## Lifecycle and locks

`TransactionManager::begin()` issues process-unique monotonic IDs. Smaller IDs are older. A transaction starts `Growing`, may acquire S or X locks, enters `Shrinking` at its first explicit unlock, and then cannot acquire locks. `commit` and `abort` release all remaining locks and transition to terminal `Committed` or `Aborted` states.

Resources are generic 64-bit `ResourceId`s; page IDs are the current binding. S/S is compatible; all pairs involving X conflict. The lock manager is mutex/condition-variable protected and maintains both resource-to-holder and transaction-to-resource tables. Waiters sleep on a condition variable, never spin.

WAIT-DIE is deterministic: a requester older than every incompatible holder waits; a younger requester aborts, releases every lock, and returns failure. This prevents wait cycles. S-to-X upgrades retain the S lock while waiting and become X only after all other shared holders are gone. Competing upgrades resolve through WAIT-DIE; X-to-S requests are rejected.

## Storage integration and limits

`TransactionalBufferPool` acquires an S/X page lock then returns move-only `LockedPage`, which unpins on destruction. Locks remain held until transaction completion. `TransactionalTableHeap` locks record pages for read/delete and the heap metadata page for insertion; `TransactionalBPlusTree` locks its metadata page for indexed operations. These wrappers are explicit to preserve the existing non-transactional Milestone 1–4 APIs.

Abort releases locks but **does not undo physical tuple/index/page changes**. Milestone 5 provides concurrency control and transactional coordination; durable undo/rollback is deferred to Milestone 6 with WAL and recovery.

## Complexity

Uncontended lock/unlock is expected O(1). Waiting depends on conflicting lock lifetime. Page locking is deliberately coarse and reduces concurrency compared with later row-level or MVCC designs.
