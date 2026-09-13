# REDO engine

`RedoEngine` consumes the Phase 2A `RecoveryState`; it never scans or decodes
the WAL independently. It considers records in their existing LSN order and
selects only transactions classified as committed. Aborted and incomplete
transactions are ignored entirely.

For a committed `PAGE_ALLOCATE`, the scoped `ReplayContext` validates an
existing physical page or materializes exactly the next contiguous slot, checks
the page format, and activates the logical allocation bit. Thus a committed
allocation target is valid before a later physical mutation can be fetched.

For committed physical mutations, REDO validates the payload-only images and
uses BufferPool fetch/modify/unpin. It applies an after-image only when the
page's LSN is lower than the mutation LSN, then sets the PageLSN exactly to that
LSN and marks the page dirty. Equal or newer PageLSNs skip safely, making REDO
idempotent. Existing WAL durability gates remain active: replay creates no WAL,
and replayed PageLSNs refer to records already present in the durable WAL.

`RedoMetrics` reports records considered, committed transactions, allocation
work/materialization, mutation work/applied/skipped counts, and duration. The
`redo_benchmark` target reports WAL size, records, mutation counts, and runtime.

UNDO is **not implemented**. Crash injection and automatic recovery are **not
implemented**. REDO does not resolve incomplete transactions or apply any
before-images.
