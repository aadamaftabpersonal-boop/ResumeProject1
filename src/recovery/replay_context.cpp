#include "ddb/recovery/replay_context.h"

#include <stdexcept>

namespace ddb::recovery {
ReplayContext::ReplayContext(ddb::storage::PageManager& pages, ddb::buffer::BufferPoolManager& pool) : pages_(pages), pool_(pool) {}
ReplayContext::~ReplayContext() noexcept { active_ = false; }
void ReplayContext::materialize_allocation_target(ddb::storage::PageId id) {
  if (!active_) throw std::logic_error("replay context is inactive");
  if (pool_.contains(id)) throw std::logic_error("cannot materialize a resident replay page");
  pages_.materialize_page_for_recovery(id);
}
}  // namespace ddb::recovery
