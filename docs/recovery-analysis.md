# Recovery analysis and replay infrastructure

Phase 2A adds read-only recovery analysis. `RecoveryAnalyzer` scans a WAL with
the existing `LogManager` decoder, so header, version, length, checksum, type,
and strictly increasing non-zero LSN validation have one implementation. It
does not open a database for normal use and `Database::analyze_wal()` exposes
this analysis-only lifecycle entry point.

The scanner accepts a truncated final suffix only: EOF before a complete fixed
header, or EOF after a valid header whose bounded declared record length cannot
be fully read. It ignores that suffix and retains prior complete records. A
malformed complete record—or corruption before later valid bytes—fails analysis;
it is never skipped.

`RecoveryState` retains every WAL record in LSN order, decoded page allocations
and physical mutations, transaction-local allocation/mutation indexes in
ascending LSN order, and a transaction classification of committed, aborted, or
incomplete. It also reports bytes scanned, record and transaction counts,
allocation/mutation counts, first/last LSN, and scan time. Analysis changes no
page data, PageLSN, buffer state, or logical allocation metadata.

`ReplayContext` is an explicit scoped recovery-only capability and has no WAL
writer. Its sole Phase 2A primitive materializes exactly the next physical page
with the normal page format, rejects invalid IDs, gaps, resident pages, and
existing physical pages, and deliberately leaves the new page logically free.
Future allocation REDO must explicitly decide when to activate it.

REDO and UNDO are **not implemented yet**. Neither automatic recovery nor crash
injection is implemented, and this phase never applies before/after images.
