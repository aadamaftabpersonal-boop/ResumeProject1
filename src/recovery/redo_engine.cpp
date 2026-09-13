#include "ddb/recovery/redo_engine.h"

#include <algorithm>
#include <stdexcept>

namespace ddb::recovery {
RedoMetrics RedoEngine::run(const RecoveryState& state, ReplayContext& replay) {
  if (!replay.active()) throw std::logic_error("REDO requires an active replay context");
  const auto started = std::chrono::steady_clock::now(); RedoMetrics metrics;
  for (const auto& [_, transaction] : state.transactions) if (transaction.status == TransactionStatus::Committed) ++metrics.committed_transactions;
  for (const auto& record : state.records) {
    ++metrics.records_considered; const auto* transaction = state.transaction(record.transaction_id);
    if (transaction == nullptr) throw std::logic_error("recovery record has no transaction analysis");
    if (transaction->status != TransactionStatus::Committed) continue;
    if (record.type == ddb::storage::WalRecordType::PageAllocate) {
      const auto allocation = std::find_if(state.allocations.begin(),state.allocations.end(),[&](const AllocationRecord& entry){return entry.lsn==record.lsn&&entry.transaction_id==record.transaction_id;});
      if (allocation==state.allocations.end()) throw std::logic_error("missing analyzed PAGE_ALLOCATE record");
      ++metrics.page_allocates_processed; if (replay.redo_page_allocate(allocation->page_id)) ++metrics.pages_materialized;
    } else if (record.type == ddb::storage::WalRecordType::PhysicalMutation) {
      const auto mutation = std::find_if(state.mutations.begin(),state.mutations.end(),[&](const MutationRecord& entry){return entry.lsn==record.lsn&&entry.transaction_id==record.transaction_id;});
      if (mutation==state.mutations.end()) throw std::logic_error("missing analyzed physical mutation");
      ++metrics.physical_mutations_considered; if (replay.redo_physical_mutation(record.lsn,mutation->mutation)) ++metrics.mutations_applied; else ++metrics.mutations_skipped_page_lsn;
    }
  }
  metrics.duration = std::chrono::steady_clock::now()-started; return metrics;
}
}  // namespace ddb::recovery
