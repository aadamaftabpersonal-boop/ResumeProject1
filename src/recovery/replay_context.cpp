#include "ddb/recovery/replay_context.h"

#include <cstring>
#include <stdexcept>

namespace ddb::recovery {
ReplayContext::ReplayContext(ddb::storage::PageManager& pages, ddb::buffer::BufferPoolManager& pool) : pages_(pages), pool_(pool) {}
ReplayContext::~ReplayContext() noexcept { active_ = false; }
bool ReplayContext::redo_page_allocate(ddb::storage::PageId id) {
  if (!active_) throw std::logic_error("replay context is inactive");
  if (!id.is_valid()) throw ddb::storage::StorageError("invalid recovery allocation page id");
  bool materialized = false;
  if (id.value() == pages_.page_count()) { if (pool_.contains(id)) throw std::logic_error("cannot materialize a resident replay page"); pages_.materialize_page_for_recovery(id); materialized = true; }
  else if (id.value() > pages_.page_count()) throw ddb::storage::StorageError("recovery allocation would create a physical gap");
  pages_.validate_physical_page_for_recovery(id);
  if (!pages_.is_page_allocated(id)) pages_.activate_reserved_page(id);
  return materialized;
}
bool ReplayContext::redo_physical_mutation(std::uint64_t lsn, const ddb::storage::PhysicalMutation& mutation) {
  if (!active_) throw std::logic_error("replay context is inactive");
  if (lsn == 0 || !mutation.page_id.is_valid() || mutation.kind != ddb::storage::MutationKind::PayloadWrite || mutation.after_image.size() != ddb::storage::kPagePayloadSize || mutation.before_image.size() != ddb::storage::kPagePayloadSize) throw std::invalid_argument("invalid recovery physical mutation");
  auto* page = pool_.fetch_page(mutation.page_id); if (page == nullptr) throw std::runtime_error("cannot pin recovery mutation page");
  if (page->lsn() >= lsn) { if (!pool_.unpin_page(mutation.page_id,false)) throw std::runtime_error("cannot unpin skipped recovery page"); return false; }
  std::memcpy(page->data(),mutation.after_image.data(),mutation.after_image.size()); page->set_lsn(lsn);
  if (!pool_.unpin_page(mutation.page_id,true)) throw std::runtime_error("cannot unpin recovery mutation page");
  return true;
}
}  // namespace ddb::recovery
