#include "ddb/concurrency/transaction.h"
#include "ddb/storage/page_manager.h"

#include <stdexcept>

namespace ddb::concurrency {

ddb::storage::MutationContext& Transaction::mutation_context(ddb::buffer::BufferPoolManager& pool) {
  if (mutation_context_ == nullptr) {
    mutation_pool_ = &pool;
    mutation_context_ = std::make_unique<ddb::storage::MutationContext>(pool, this);
  } else if (mutation_pool_ != &pool) {
    throw std::logic_error("a transaction cannot span buffer pools");
  }
  return *mutation_context_;
}

ddb::storage::PageId Transaction::allocate_page(ddb::storage::PageManager& pages) {
  const auto page=log_sink_ == nullptr ? pages.allocate_page() : log_sink_->allocate_page(pages, id_);
  allocation_manager_=&pages; allocated_pages_.push_back(page); return page;
}

std::optional<std::uint64_t> Transaction::finalize_mutation(ddb::storage::PhysicalMutation mutation) {
  pending_mutations_.push_back(std::move(mutation));
  undo_resolved_.push_back(false);
  if (log_sink_ != nullptr) {
    const auto lsn = log_sink_->log_physical_mutation(id_, pending_mutations_.back());
    if (lsn == 0) throw std::logic_error("WAL mutation logger returned zero LSN");
    ++finalized_mutations_;
    return lsn;
  }
  return std::nullopt;
}

bool Transaction::has_unfinalized_mutations() const noexcept {
  return mutation_context_ != nullptr && mutation_context_->has_unfinished_capture()
      || finalized_mutations_ != pending_mutations_.size();
}

bool Transaction::has_unresolved_mutations_for_abort() const noexcept {
  if (mutation_context_ != nullptr && mutation_context_->has_unfinished_capture()) return true;
  for (std::size_t index = 0; index < pending_mutations_.size(); ++index) {
    if (!undo_resolved_[index]) return true;
  }
  return false;
}

void Transaction::finalize_next_mutation(std::uint64_t lsn) {
  if (mutation_pool_ == nullptr || finalized_mutations_ == pending_mutations_.size()) {
    throw std::logic_error("no captured transaction mutation to finalize");
  }
  const auto& mutation = pending_mutations_[finalized_mutations_];
  mutation_pool_->finalize_pending_mutation(mutation.page_id, lsn);
  ++finalized_mutations_;
}

void Transaction::resolve_mutation_for_undo(std::size_t mutation_index, std::uint64_t restored_page_lsn) {
  if (mutation_pool_ == nullptr || mutation_index >= pending_mutations_.size()) {
    throw std::logic_error("invalid transaction mutation for undo resolution");
  }
  if (undo_resolved_[mutation_index]) throw std::logic_error("mutation was already resolved for undo");
  for (std::size_t later = mutation_index + 1; later < undo_resolved_.size(); ++later) {
    if (!undo_resolved_[later]) throw std::logic_error("undo resolution must follow reverse mutation order");
  }
  const auto page_id = pending_mutations_[mutation_index].page_id;
  if (mutation_index < finalized_mutations_) {
    mutation_pool_->restore_page_lsn_after_undo(page_id, restored_page_lsn);
  } else {
    mutation_pool_->resolve_pending_mutation(page_id, restored_page_lsn);
  }
  undo_resolved_[mutation_index] = true;
}

void Transaction::rollback_for_abort() {
  if (mutation_context_ != nullptr && mutation_context_->has_unfinished_capture()) throw std::logic_error("cannot abort unfinished physical mutation capture");
  for (std::size_t index=pending_mutations_.size();index>0;--index) {
    const auto mutation_index=index-1; if (undo_resolved_[mutation_index]) continue;
    if (mutation_pool_ == nullptr) throw std::logic_error("transaction mutation has no buffer pool");
    ddb::storage::apply_before_image(*mutation_pool_,pending_mutations_[mutation_index]);
    resolve_mutation_for_undo(mutation_index,pending_mutations_[mutation_index].previous_page_lsn);
  }
  if (allocation_manager_ != nullptr) for(auto it=allocated_pages_.rbegin();it!=allocated_pages_.rend();++it) if(allocation_manager_->is_page_allocated(*it)) allocation_manager_->deallocate_page(*it);
}

}  // namespace ddb::concurrency
