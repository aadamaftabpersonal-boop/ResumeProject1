#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "ddb/storage/physical_mutation.h"

namespace ddb::storage { class PageManager; }

namespace ddb::concurrency {
using TransactionId = std::uint64_t;
enum class TransactionState : std::uint8_t { Growing, Shrinking, Committed, Aborted };

class TransactionLogSink {
 public:
  virtual ~TransactionLogSink() = default;
  virtual void log_begin(TransactionId) = 0;
  [[nodiscard]] virtual std::uint64_t log_physical_mutation(TransactionId, const ddb::storage::PhysicalMutation&) = 0;
  // Delegates to the storage-owned transactional allocator so transaction
  // clients cannot accidentally reorder reserve/WAL-durability/activation.
  [[nodiscard]] virtual ddb::storage::PageId allocate_page(ddb::storage::PageManager&, TransactionId) = 0;
};

class Transaction final : public ddb::storage::MutationFinalizer {
 public:
  [[nodiscard]] TransactionId id() const noexcept { return id_; }
  [[nodiscard]] TransactionState state() const noexcept { return state_.load(); }
  [[nodiscard]] ddb::storage::MutationContext& mutation_context(ddb::buffer::BufferPoolManager&);
  [[nodiscard]] ddb::storage::PageId allocate_page(ddb::storage::PageManager&);
  [[nodiscard]] const std::vector<ddb::storage::PhysicalMutation>& pending_mutations() const noexcept { return pending_mutations_; }
  [[nodiscard]] bool has_unfinalized_mutations() const noexcept;
  [[nodiscard]] bool has_unresolved_mutations_for_abort() const noexcept;
  // Called by the future durability coordinator after it has appended the
  // mutation record and obtained its real, nonzero LSN. Finalization is
  // strictly capture order and does not copy page images.
  void finalize_next_mutation(std::uint64_t lsn);
  // Called only after the caller has applied the mutation's before image.
  // Indices must be resolved in reverse capture order. The supplied PageLSN
  // is the most recent surviving mutation LSN for this same page, or zero.
  void resolve_mutation_for_undo(std::size_t mutation_index, std::uint64_t restored_page_lsn);
  // Runtime abort is a physical rollback, unlike crash recovery's page-history
  // reconstruction.  The transaction owns both its captured images and the
  // pre-mutation PageLSNs needed for this reverse operation.
  void rollback_for_abort();
  [[nodiscard]] std::optional<std::uint64_t> finalize_mutation(ddb::storage::PhysicalMutation mutation) override;
 private:
  friend class TransactionManager;
  friend class LockManager;
  explicit Transaction(TransactionId id) : id_(id) {}
  Transaction(TransactionId id, TransactionLogSink* log_sink) : id_(id), log_sink_(log_sink) {}
  TransactionId id_;
  std::atomic<TransactionState> state_{TransactionState::Growing};
  ddb::buffer::BufferPoolManager* mutation_pool_{nullptr};
  std::unique_ptr<ddb::storage::MutationContext> mutation_context_;
  std::vector<ddb::storage::PhysicalMutation> pending_mutations_;
  std::vector<bool> undo_resolved_;
  std::size_t finalized_mutations_{0};
  TransactionLogSink* log_sink_{nullptr};
  ddb::storage::PageManager* allocation_manager_{nullptr};
  std::vector<ddb::storage::PageId> allocated_pages_;
};

class TransactionManager final {
 public:
  explicit TransactionManager(TransactionLogSink* log_sink = nullptr) : log_sink_(log_sink) {}
  [[nodiscard]] Transaction begin() { const auto id = next_id_.fetch_add(1); if (log_sink_ != nullptr) log_sink_->log_begin(id); return Transaction(id, log_sink_); }
 private:
  std::atomic<TransactionId> next_id_{1}; // Smaller ID means older under WAIT-DIE.
  TransactionLogSink* log_sink_{nullptr};
};
}
