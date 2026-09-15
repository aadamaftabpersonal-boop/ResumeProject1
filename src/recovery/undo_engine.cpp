#include "ddb/recovery/undo_engine.h"
#include <algorithm>
#include <map>
#include <set>
namespace ddb::recovery { namespace {
}
UndoMetrics UndoEngine::run(const RecoveryState& state,ReplayContext& replay){if(!replay.active())throw std::logic_error("UNDO requires active replay context");const auto started=std::chrono::steady_clock::now();UndoMetrics metrics;std::vector<const MutationRecord*> mutations;std::vector<const AllocationRecord*> allocations;
for(const auto& [_,tx]:state.transactions){if(tx.status==TransactionStatus::Committed){++metrics.committed_transactions_skipped;continue;}if(tx.status==TransactionStatus::Aborted)++metrics.aborted_transactions;else ++metrics.incomplete_transactions;for(auto index:tx.mutation_indexes)mutations.push_back(&state.mutations.at(index));for(auto index:tx.allocation_indexes)allocations.push_back(&state.allocations.at(index));}
// Full-page images retain enough information to reconstruct a page, but a
// committed after-image may contain unchanged loser bytes.  Apply only the
// byte delta (before -> after) of each committed mutation to the earliest
// durable base image; copying its full after-image would resurrect a loser.
std::map<std::uint64_t,std::vector<const MutationRecord*>> histories;for(const auto& mutation:state.mutations)histories[mutation.mutation.page_id.value()].push_back(&mutation);
std::set<std::uint64_t> affected;for(const auto* mutation:mutations)affected.insert(mutation->mutation.page_id.value());
for(const auto page_value:affected){auto& history=histories.at(page_value);std::sort(history.begin(),history.end(),[](auto a,auto b){return a->lsn<b->lsn;});std::vector<std::byte> image=history.front()->mutation.before_image;std::uint64_t survivor_lsn{};for(const auto* entry:history){const auto*tx=state.transaction(entry->transaction_id);if(tx==nullptr)throw std::logic_error("mutation without transaction");if(tx->status!=TransactionStatus::Committed)continue;for(std::size_t i=0;i<image.size();++i)if(entry->mutation.before_image[i]!=entry->mutation.after_image[i])image[i]=entry->mutation.after_image[i];survivor_lsn=entry->lsn;}const auto id=ddb::storage::PageId(page_value);if(replay.reconstruct_page(id,image,survivor_lsn)){for(const auto*entry:history)if(state.transaction(entry->transaction_id)->status!=TransactionStatus::Committed)++metrics.mutations_undone;metrics.pages_touched++;}else metrics.mutations_skipped+=static_cast<std::size_t>(std::count_if(history.begin(),history.end(),[&](const auto*entry){return state.transaction(entry->transaction_id)->status!=TransactionStatus::Committed;}));}
std::sort(allocations.begin(),allocations.end(),[](auto a,auto b){return a->lsn>b->lsn;});for(const auto* allocation:allocations){++metrics.records_considered;if(replay.undo_page_allocate(allocation->page_id))++metrics.allocations_released;}
metrics.records_considered=mutations.size()+allocations.size();metrics.mutations_considered=mutations.size();metrics.duration=std::chrono::steady_clock::now()-started;return metrics;}
}
