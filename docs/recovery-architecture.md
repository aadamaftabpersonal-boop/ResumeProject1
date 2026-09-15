# Recovery architecture

## Runtime transaction abort

An explicit abort has the live transaction, captured physical images, buffer
frames, and predecessor PageLSNs. It rolls mutations back in reverse capture
order through `BufferPoolManager`, restores the captured predecessor PageLSN,
logically releases transaction-owned allocations, and only then writes and
durably flushes `ABORT`. The coordinator releases locks afterwards.

## Crash recovery

Crash recovery can see a page containing committed and loser changes. It must
not reverse-write a loser before-image over a later committed PageLSN.

For each page touched by a loser, recovery builds a base from the earliest WAL
before-image. In LSN order it applies only byte deltas (`before` to `after`) of
committed mutations. Copying a complete committed after-image is unsafe because
it can contain unchanged bytes written by a loser. The result has the newest
surviving committed LSN, or zero. Replay remains WAL-free and uses only
`ReplayContext` and `BufferPoolManager`.
