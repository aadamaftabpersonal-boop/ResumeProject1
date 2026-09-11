# Local WAL and recovery

## Phase 1 status

This document describes Phase 1 only. Persistent local WAL, physical mutation
records, PageLSN assignment, and durable commit are implemented. Analysis,
REDO, UNDO, crash recovery, checkpoints, and crash injection are deliberately
not implemented yet.

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

Explicit abort records can be emitted only for transactions with no unresolved
physical mutations. Physical abort undo and all crash recovery are Phase 2
work; no recovery claim is made by this phase.
