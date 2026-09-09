#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ddb/concurrency/transaction.h"
#include "ddb/storage/page.h"

namespace ddb::concurrency {
struct ResourceId final {
  std::uint64_t value;
  explicit ResourceId(ddb::storage::PageId page) : value(page.value()) {}
  explicit ResourceId(std::uint64_t raw) : value(raw) {}
  friend bool operator==(ResourceId, ResourceId) = default;
};
struct ResourceIdHash final { [[nodiscard]] std::size_t operator()(ResourceId id) const noexcept { return std::hash<std::uint64_t>{}(id.value); } };
enum class LockMode : std::uint8_t { Shared, Exclusive };

class LockManager final {
 public:
  [[nodiscard]] bool lock_shared(Transaction&, ResourceId);
  [[nodiscard]] bool lock_exclusive(Transaction&, ResourceId); // S->X upgrade when already shared.
  [[nodiscard]] bool unlock(Transaction&, ResourceId);
  void commit(Transaction&);
  void abort(Transaction&);
  [[nodiscard]] std::size_t lock_count() const;
  [[nodiscard]] std::size_t held_lock_count(const Transaction&) const;
  [[nodiscard]] bool holds(const Transaction&, ResourceId, LockMode) const;
 private:
  struct Holder final { TransactionId transaction_id; LockMode mode; };
  [[nodiscard]] bool lock(Transaction&, ResourceId, LockMode);
  [[nodiscard]] bool compatible(const Transaction&, ResourceId, LockMode) const;
  [[nodiscard]] bool requester_is_younger_than_conflict(const Transaction&, ResourceId, LockMode) const;
  void release_all(Transaction&);
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::unordered_map<ResourceId, std::vector<Holder>, ResourceIdHash> table_;
  std::unordered_map<TransactionId, std::unordered_set<ResourceId, ResourceIdHash>> held_;
};
}
