# Logical page allocation

The database begins with one 4096-byte storage metadata region, followed by ordinary versioned pages. Application PageId 0 therefore remains the first ordinary page. The metadata region has `DDBA` magic, version 1, physical-page and allocated-page counts, and a fixed bitmap; it supports 32,576 physical application pages.

Physical presence and logical allocation are distinct. A normal read/write/fetch requires both a physical page in range and its bitmap bit set. Deallocation clears only the bitmap bit: bytes remain in place and pages are not reused. This preserves contiguous physical IDs while allowing a later recovery protocol to retain an incomplete allocation's slot but make it inaccessible.

New databases create the catalog before application pages. Files lacking the catalog are rejected as legacy/corrupt; no implicit migration or reinterpretation occurs. Catalog count/bounds or magic/version corruption also fails opening.

This is only the prerequisite for future WAL allocation. PAGE_ALLOCATE WAL support, PAGE_FREE WAL support, REDO, UNDO, checkpoints, and crash recovery are **not implemented**. The future protocol will durably log allocation before resolving its bitmap bit, while BufferPool access remains protected by PageManager validation.
