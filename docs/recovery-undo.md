# UNDO engine

`UndoEngine` consumes `RecoveryState` directly and identifies pages touched by
aborted or incomplete transactions. It reconstructs those page histories
through the scoped, WAL-free `ReplayContext`; committed changes survive.

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

## Page-history reconstruction

The description above was superseded by page-history-aware crash recovery.
For every page touched by an aborted or incomplete transaction, recovery starts
from the earliest full before-image and applies only the byte deltas of
committed mutations in LSN order. A complete committed after-image cannot be
copied wholesale because it may contain unchanged bytes written by a loser.
The reconstructed PageLSN is the latest surviving committed LSN, or zero.

Runtime abort is separate: it reverses a live transaction's captured images
through the BufferPool, restores the captured predecessor PageLSNs, releases
its logical allocations, and only then durably appends `ABORT`.
