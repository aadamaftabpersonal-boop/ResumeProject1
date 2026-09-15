#pragma once

#include <unordered_set>
#include "ddb/storage/physical_mutation.h"
#include "ddb/buffer/buffer_pool_manager.h"
#include "ddb/storage/page_manager.h"

namespace ddb::recovery {
// Scoped, WAL-free recovery capability. This phase intentionally provides no
// image-application API; later REDO/UNDO will be explicit additions here.
class ReplayContext final {
 public:
  ReplayContext(ddb::storage::PageManager&, ddb::buffer::BufferPoolManager&);
  ~ReplayContext() noexcept;
  ReplayContext(const ReplayContext&) = delete;
  ReplayContext& operator=(const ReplayContext&) = delete;
  [[nodiscard]] bool active() const noexcept { return active_; }
  // Returns true only when a physical slot was created during this call.
  [[nodiscard]] bool redo_page_allocate(ddb::storage::PageId);
  // Returns true when the after-image was applied; false means PageLSN made it
  // idempotently unnecessary. It never appends WAL.
  [[nodiscard]] bool redo_physical_mutation(std::uint64_t lsn, const ddb::storage::PhysicalMutation&);
  [[nodiscard]] bool undo_physical_mutation(std::uint64_t lsn, std::uint64_t restored_lsn, const ddb::storage::PhysicalMutation&);
  [[nodiscard]] bool undo_page_allocate(ddb::storage::PageId);
  // Replaces a page payload with a recovery-derived image without consulting
  // the normal PageLSN skip rule.  The image has already incorporated all
  // surviving per-page history.
  [[nodiscard]] bool reconstruct_page(ddb::storage::PageId, const std::vector<std::byte>&, std::uint64_t page_lsn);
 private:
  ddb::storage::PageManager& pages_;
  ddb::buffer::BufferPoolManager& pool_;
  bool active_{true};
  std::unordered_set<std::uint64_t> undone_lsns_;
  std::unordered_set<std::uint64_t> reconstructed_pages_;
};
}  // namespace ddb::recovery
