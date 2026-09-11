#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "ddb/storage/physical_mutation.h"

namespace ddb::concurrency {
using TransactionId = std::uint64_t;
enum class TransactionState : std::uint8_t { Growing, Shrinking, Committed, Aborted };

class Transaction final : public ddb::storage::MutationFinalizer {
 public:
  [[nodiscard]] TransactionId id() const noexcept { return id_; }
  [[nodiscard]] TransactionState state() const noexcept { return state_.load(); }
  [[nodiscard]] ddb::storage::MutationContext& mutation_context(ddb::buffer::BufferPoolManager&);
  [[nodiscard]] const std::vector<ddb::storage::PhysicalMutation>& pending_mutations() const noexcept { return pending_mutations_; }
  [[nodiscard]] bool has_unfinalized_mutations() const noexcept;
  // Called by the future durability coordinator after it has appended the
  // mutation record and obtained its real, nonzero LSN. Finalization is
  // strictly capture order and does not copy page images.
  void finalize_next_mutation(std::uint64_t lsn);
  [[nodiscard]] std::optional<std::uint64_t> finalize_mutation(ddb::storage::PhysicalMutation mutation) override;
 private:
  friend class TransactionManager;
  friend class LockManager;
  explicit Transaction(TransactionId id) : id_(id) {}
  TransactionId id_;
  std::atomic<TransactionState> state_{TransactionState::Growing};
  ddb::buffer::BufferPoolManager* mutation_pool_{nullptr};
  std::unique_ptr<ddb::storage::MutationContext> mutation_context_;
  std::vector<ddb::storage::PhysicalMutation> pending_mutations_;
  std::size_t finalized_mutations_{0};
};

class TransactionManager final {
 public:
  [[nodiscard]] Transaction begin() { return Transaction(next_id_.fetch_add(1)); }
 private:
  std::atomic<TransactionId> next_id_{1}; // Smaller ID means older under WAIT-DIE.
};
}
