# UNDO engine

`UndoEngine` consumes `RecoveryState` directly and selects only aborted and
incomplete transactions. Their physical mutations are processed in descending
LSN order through the scoped, WAL-free `ReplayContext`; committed transactions
are never undone.

UNDO restores payload-only before-images through `BufferPoolManager`, marks
pages dirty, and restores PageLSN with the existing undo-specific BufferPool
API. The restored value is the immediately preceding same-page mutation LSN,
or zero if none exists—never `LSN - 1` or a global predecessor. A newer
unexpected PageLSN is rejected rather than overwritten.

Uncommitted `PAGE_ALLOCATE` targets are retained physically but deactivated
logically after their mutations are undone, so ordinary fetch rejects them.
Replay never appends or changes WAL. Per-context resolved-LSN tracking makes a
repeat UNDO invocation safe.

REDO remains separate. Automatic recovery, crash injection, checkpoints,
PAGE_FREE, and distributed recovery are not implemented.
