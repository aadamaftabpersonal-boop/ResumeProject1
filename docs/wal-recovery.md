# Local WAL and recovery

## Phase 2A status

Persistent local WAL, physical mutation records, PageLSN assignment, durable
commit, and read-only recovery analysis are implemented. REDO, UNDO, automatic
crash recovery, checkpoints, and crash injection are deliberately not
implemented yet. See `recovery-analysis.md` for analysis and replay scaffolding.

## File and format

Each database uses `<database>.wal`. Records are little-endian and explicitly
encoded: `DWAL` magic (4 bytes), version (u16), header length (u16), total
record length (u32), LSN (u64), transaction ID (u64), type (u8), reserved bytes
(3), payload length (u32), payload, then an FNV-1a u32 checksum over everything
before the checksum. The fixed header is 36 bytes. No C++ structure is written
directly.

Record types are BEGIN, PHYSICAL_MUTATION, COMMIT, and ABORT. A physical record
contains PageId, mutation kind and sequence, both image lengths, then the
existing payload-only before and after images.

## Durability and LSNs

LSNs start at one. Opening scans complete, checksummed records, uses the
highest valid LSN plus one, and drops only a truncated final suffix before a new
append. Invalid complete magic, versions, lengths, checksum, types, and
non-monotonic LSNs fail opening. `flush()` uses `FlushFileBuffers` on Windows
before advancing the exposed durable LSN.

A transaction-owned mutation is appended before its PageLSN is assigned. The
buffer pool therefore refuses page write-back until the WAL durable LSN reaches
PageLSN. Commit appends COMMIT and flushes the WAL before the coordinator marks
the transaction committed or releases locks.

## Current limitations

Explicit abort rolls back its live physical mutations and allocations before
the `ABORT` record is appended and durably flushed. Automatic lifecycle
integration remains outside this architecture change.

## Abort and page history

Explicit abort first rolls back a live transaction through the BufferPool and
only then makes its `ABORT` durable. Crash recovery instead reconstructs pages
from WAL image history, preserving later committed byte changes while removing
loser changes. Image application in either path emits no WAL.
