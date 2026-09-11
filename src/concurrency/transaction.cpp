#include "ddb/concurrency/transaction.h"

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

std::optional<std::uint64_t> Transaction::finalize_mutation(ddb::storage::PhysicalMutation mutation) {
  pending_mutations_.push_back(std::move(mutation));
  return std::nullopt;
}

bool Transaction::has_unfinalized_mutations() const noexcept {
  return mutation_context_ != nullptr && mutation_context_->has_unfinished_capture()
      || finalized_mutations_ != pending_mutations_.size();
}

void Transaction::finalize_next_mutation(std::uint64_t lsn) {
  if (mutation_pool_ == nullptr || finalized_mutations_ == pending_mutations_.size()) {
    throw std::logic_error("no captured transaction mutation to finalize");
  }
  const auto& mutation = pending_mutations_[finalized_mutations_];
  mutation_pool_->finalize_pending_mutation(mutation.page_id, lsn);
  ++finalized_mutations_;
}

}  // namespace ddb::concurrency
