# Query execution design

```text
Plan tree -> Executor -> TableHeap / BPlusTree -> BufferPoolManager -> PageManager
```

The execution layer never reads the database file. `TableHeap` and `BPlusTree` obtain persistent pages exclusively from the buffer pool and unpin them as soon as an operation no longer needs them.

## Values and tuples

`Schema` owns ordered `Column{name, type}` definitions. A `Tuple` owns a vector of `Value` variants: `int64_t`, `double`, `bool`, or `string`. Heap insertion validates both column count and variant type. Tuple bytes contain schema-order values: eight little-endian bytes for integer/double, one byte for boolean, and a four-byte little-endian length followed by string bytes. The schema is supplied when reopening a heap; catalog persistence is intentionally future work.

## Table heap and slotted pages

`RecordId` is the B+ tree-compatible physical pair `{ PageId, SlotId }`. A table metadata page holds a magic value and the first/last data-page IDs. Data pages are distinct from index pages and have this layout:

```text
+-------------------------------+
| magic | next page | slot count |
| slot-dir-end | tuple-dir-start |
+-------------------------------+
| slot: offset, length, live flag|
| ...                           |
|            free space          |
| ... tuple bytes (backward)     |
+-------------------------------+
```

Slots are never renumbered, so deletes only clear the live flag and old `RecordId`s do not become aliases. Deletion does not compact bytes or reclaim free space in this milestone. A scan pins one page, materializes its live tuples, unpins it, and advances. This keeps large scans compatible with small buffer pools.

## Plans and executors

Plans are immutable composable objects: `SeqScanPlan`, `IndexScanPlan`, `FilterPlan`, `ProjectionPlan`, `SortPlan`, `AggregatePlan`, and `JoinPlan`. `explain()` prints an indented tree. `make_executor(plan)` recursively constructs iterator executors using `init()` followed by repeated `next(tuple)` calls.

```text
Projection[1 2]
  Filter[column 0 >]
    SeqScan[users]
```

Filter supports `= != < <= > >=`; projection preserves requested order. Sort is stable and supports multiple ASC/DESC keys, but materializes its entire child. Aggregate materializes input, supports COUNT, SUM, MIN, MAX, AVG and optional grouping columns. SUM/AVG produce `double` and require numeric values. Nested-loop join materializes the right child and produces concatenated tuples for equality matches; duplicates are relationally significant. `IndexScan` currently provides equality lookup: B+ tree key -> `RecordId` -> heap tuple.

## Complexity and limitations

Seq scan, filter, and projection are O(N); in-memory sort is O(N log N); aggregate is O(N) plus grouping comparisons; nested-loop join is O(N*M); index equality lookup is O(log N). There is no SQL parser, catalog, null value, transactions, locking, WAL/recovery, optimizer, external sort, hash/merge join, or index range executor. Index keys are the existing signed 64-bit B+ tree keys and are unique.

`validate_invariants()` checks page header bounds, slot-directory placement, live tuple bounds/deserialization, and the heap chain. Tests additionally check index-to-heap correspondence and buffer-pool pin counts after execution.
