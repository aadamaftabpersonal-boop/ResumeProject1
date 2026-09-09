#pragma once
#include <atomic>
#include <cstdint>

namespace ddb::concurrency {
using TransactionId = std::uint64_t;
enum class TransactionState : std::uint8_t { Growing, Shrinking, Committed, Aborted };

class Transaction final {
 public:
  [[nodiscard]] TransactionId id() const noexcept { return id_; }
  [[nodiscard]] TransactionState state() const noexcept { return state_.load(); }
 private:
  friend class TransactionManager;
  friend class LockManager;
  explicit Transaction(TransactionId id) : id_(id) {}
  TransactionId id_;
  std::atomic<TransactionState> state_{TransactionState::Growing};
};

class TransactionManager final {
 public:
  [[nodiscard]] Transaction begin() { return Transaction(next_id_.fetch_add(1)); }
 private:
  std::atomic<TransactionId> next_id_{1}; // Smaller ID means older under WAIT-DIE.
};
}
