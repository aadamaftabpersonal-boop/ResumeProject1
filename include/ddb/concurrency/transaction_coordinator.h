#pragma once

#include "ddb/concurrency/lock_manager.h"

namespace ddb::concurrency {

// Future WAL/recovery code implements these hooks.  LockManager remains solely
// responsible for lock ownership and terminal-state lock release.
class TransactionDurabilityParticipant {
 public:
  virtual ~TransactionDurabilityParticipant() = default;
  // Implementations must resolve every captured mutation before returning:
  // commit by logging/finalizing it, abort by undoing/finalizing cleanup.
  virtual void prepare_commit(Transaction&) = 0;
  virtual void prepare_abort(Transaction&) = 0;
};

class TransactionCoordinator final {
 public:
  explicit TransactionCoordinator(LockManager&, TransactionDurabilityParticipant* = nullptr);
  void commit(Transaction&);
  void abort(Transaction&);
 private:
  LockManager& locks_;
  TransactionDurabilityParticipant* participant_;
};

}  // namespace ddb::concurrency
