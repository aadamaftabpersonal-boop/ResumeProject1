#include "ddb/recovery/undo_engine.h"
#include <algorithm>
#include <set>
namespace ddb::recovery { namespace {
bool eligible(const TransactionAnalysis& tx){return tx.status==TransactionStatus::Aborted||tx.status==TransactionStatus::Incomplete;}
std::uint64_t prior_page_lsn(const RecoveryState& state,const MutationRecord& current){std::uint64_t prior{};for(const auto& candidate:state.mutations)if(candidate.mutation.page_id==current.mutation.page_id&&candidate.lsn<current.lsn&&candidate.lsn>prior)prior=candidate.lsn;return prior;}
}
UndoMetrics UndoEngine::run(const RecoveryState& state,ReplayContext& replay){if(!replay.active())throw std::logic_error("UNDO requires active replay context");const auto started=std::chrono::steady_clock::now();UndoMetrics metrics;std::vector<const MutationRecord*> mutations;std::vector<const AllocationRecord*> allocations;
for(const auto& [_,tx]:state.transactions){if(tx.status==TransactionStatus::Committed){++metrics.committed_transactions_skipped;continue;}if(tx.status==TransactionStatus::Aborted)++metrics.aborted_transactions;else ++metrics.incomplete_transactions;for(auto index:tx.mutation_indexes)mutations.push_back(&state.mutations.at(index));for(auto index:tx.allocation_indexes)allocations.push_back(&state.allocations.at(index));}
std::sort(mutations.begin(),mutations.end(),[](auto a,auto b){return a->lsn>b->lsn;});std::set<std::uint64_t> touched;for(const auto* mutation:mutations){++metrics.records_considered;++metrics.mutations_considered;if(replay.undo_physical_mutation(mutation->lsn,prior_page_lsn(state,*mutation),mutation->mutation)){++metrics.mutations_undone;touched.insert(mutation->mutation.page_id.value());}else ++metrics.mutations_skipped;}
std::sort(allocations.begin(),allocations.end(),[](auto a,auto b){return a->lsn>b->lsn;});for(const auto* allocation:allocations){++metrics.records_considered;if(replay.undo_page_allocate(allocation->page_id))++metrics.allocations_released;}
metrics.pages_touched=touched.size();metrics.duration=std::chrono::steady_clock::now()-started;return metrics;}
}
