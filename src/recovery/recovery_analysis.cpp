#include "ddb/recovery/recovery_analysis.h"

#include <filesystem>
#include <stdexcept>

namespace ddb::recovery {
namespace {
void validate_empty_payload(const ddb::storage::WalRecord& record) {
  if (!record.payload.empty()) throw ddb::storage::WalError("terminal WAL record has a payload");
}
}

const TransactionAnalysis* RecoveryState::transaction(std::uint64_t id) const noexcept {
  const auto found = transactions.find(id); return found == transactions.end() ? nullptr : &found->second;
}
std::vector<const AllocationRecord*> RecoveryState::allocations_for_page(ddb::storage::PageId page) const {
  std::vector<const AllocationRecord*> result; for (const auto& allocation : allocations) if (allocation.page_id == page) result.push_back(&allocation); return result;
}
std::vector<const MutationRecord*> RecoveryState::mutations_for_page(ddb::storage::PageId page) const {
  std::vector<const MutationRecord*> result; for (const auto& mutation : mutations) if (mutation.mutation.page_id == page) result.push_back(&mutation); return result;
}

RecoveryState RecoveryAnalyzer::analyze(const ddb::storage::LogManager& log) { return analyze(log.path()); }
RecoveryState RecoveryAnalyzer::analyze(const std::filesystem::path& wal_path) {
  const auto started = std::chrono::steady_clock::now();
  RecoveryState state; state.records = ddb::storage::LogManager::read_records(wal_path);
  std::error_code error; state.metrics.wal_bytes_scanned = std::filesystem::file_size(wal_path, error); if (error) throw ddb::storage::WalError("cannot determine WAL size: " + error.message());
  for (const auto& record : state.records) {
    auto& transaction = state.transactions[record.transaction_id]; transaction.transaction_id = record.transaction_id;
    switch (record.type) {
      case ddb::storage::WalRecordType::Begin: validate_empty_payload(record); transaction.began = true; break;
      case ddb::storage::WalRecordType::Commit: validate_empty_payload(record); transaction.committed = true; break;
      case ddb::storage::WalRecordType::Abort: validate_empty_payload(record); transaction.aborted = true; break;
      case ddb::storage::WalRecordType::PageAllocate: {
        const auto page = ddb::storage::LogManager::decode_page_allocate(record.payload);
        transaction.allocation_indexes.push_back(state.allocations.size()); state.allocations.push_back({record.lsn, record.transaction_id, page}); break;
      }
      case ddb::storage::WalRecordType::PhysicalMutation: {
        auto mutation = ddb::storage::LogManager::decode_physical_mutation(record.payload);
        transaction.mutation_indexes.push_back(state.mutations.size()); state.mutations.push_back({record.lsn, record.transaction_id, std::move(mutation)}); break;
      }
      default: throw ddb::storage::WalError("unknown WAL record type during recovery analysis");
    }
  }
  for (auto& [_, transaction] : state.transactions) {
    if (transaction.committed && transaction.aborted) throw ddb::storage::WalError("transaction has conflicting terminal WAL records");
    transaction.status = transaction.committed ? TransactionStatus::Committed : transaction.aborted ? TransactionStatus::Aborted : TransactionStatus::Incomplete;
    if (transaction.status == TransactionStatus::Committed) ++state.metrics.committed_transactions;
    else if (transaction.status == TransactionStatus::Aborted) ++state.metrics.aborted_transactions;
    else ++state.metrics.incomplete_transactions;
  }
  state.metrics.total_records = state.records.size(); state.metrics.transactions_observed = state.transactions.size(); state.metrics.page_allocates = state.allocations.size(); state.metrics.physical_mutations = state.mutations.size();
  if (!state.records.empty()) { state.metrics.first_lsn = state.records.front().lsn; state.metrics.last_lsn = state.records.back().lsn; }
  state.metrics.scan_duration = std::chrono::steady_clock::now() - started;
  return state;
}
}  // namespace ddb::recovery
