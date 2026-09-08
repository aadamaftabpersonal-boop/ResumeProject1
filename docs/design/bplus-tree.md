# Milestone 3: persistent B+ tree

## Structure

```text
metadata page -> current root
                    |
              internal pages
                    |
          leaf pages linked forward
```

The metadata page stores a magic value, root `PageId`, and node capacities. It is supplied when reopening; the root is never assumed to be page zero. All page reads and writes use `BufferPoolManager`; `PageManager` is used only to allocate a new physical page.

`KeyType` is a signed 64-bit integer. `RecordId` is `{ PageId, uint32_t slot_id }`. Duplicate inserts return `false` and leave the existing value unchanged.

## Explicit page encoding

Nodes use byte-wise little-endian encoding, not object serialization. A 32-byte header contains type (leaf/internal), parent ID, next-leaf ID, and key count. A leaf entry is `int64 key | uint64 record-page | uint32 slot` (20 bytes). An internal node stores child zero followed by `int64 separator | uint64 child` pairs (16 bytes). The configured capacities are checked to fit in 4096 bytes.

Internal separator `keys[i]` equals the first key of `children[i+1]`; an internal page with N keys has N+1 children. Leaves carry all key/value records and `next` points to the following leaf.

## Algorithms and invariants

Lookup descends using upper-bound separator selection. Insertion splits a full leaf, links its new right sibling, and inserts that sibling's first key into the parent. Internal splits promote the middle separator and rewrite affected child parent pointers. A root split creates a new persisted root and updates metadata.

Deletion first attempts sibling redistribution, otherwise merges pages and repairs parent underflow recursively. A one-child internal root contracts to its child. Changed leftmost-subtree minima propagate up every first-child ancestor so all separators remain correct.

The checker verifies sorted keys, child count, separator-to-subtree-minimum correctness, parent pointers, reachability, unique keys, forward leaf-chain order, and buffer-pool invariants. The implementation is single-threaded and has no crash-atomic structural update protocol; WAL/recovery is deferred.

## Complexity

Point lookup, insertion, and deletion are O(log N). A range scan is O(log N + K) because it finds the first leaf then follows leaf links for K results. Page I/O remains filesystem-dependent.
