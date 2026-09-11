#include "ddb/concurrency/transaction_coordinator.h"

namespace ddb::concurrency {

TransactionCoordinator::TransactionCoordinator(LockManager& locks, TransactionDurabilityParticipant* participant)
    : locks_(locks), participant_(participant) {}

void TransactionCoordinator::commit(Transaction& transaction) {
  if (transaction.state() == TransactionState::Committed || transaction.state() == TransactionState::Aborted) return;
  if (participant_ != nullptr) participant_->prepare_commit(transaction);
  if (transaction.has_unfinalized_mutations()) {
    throw std::logic_error("cannot commit transaction with unfinalized physical mutations");
  }
  locks_.commit(transaction);
}

void TransactionCoordinator::abort(Transaction& transaction) {
  if (transaction.state() == TransactionState::Committed || transaction.state() == TransactionState::Aborted) return;
  if (participant_ != nullptr) participant_->prepare_abort(transaction);
  if (transaction.has_unfinalized_mutations()) {
    throw std::logic_error("cannot abort transaction with unresolved physical mutations");
  }
  locks_.abort(transaction);
}

}  // namespace ddb::concurrency
