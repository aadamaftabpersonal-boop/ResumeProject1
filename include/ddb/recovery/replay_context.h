#pragma once

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
  void materialize_allocation_target(ddb::storage::PageId);
 private:
  ddb::storage::PageManager& pages_;
  ddb::buffer::BufferPoolManager& pool_;
  bool active_{true};
};
}  // namespace ddb::recovery
