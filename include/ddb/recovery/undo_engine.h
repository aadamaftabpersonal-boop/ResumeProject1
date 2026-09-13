#pragma once
#include <chrono>
#include <cstddef>
#include "ddb/recovery/recovery_analysis.h"
#include "ddb/recovery/replay_context.h"
namespace ddb::recovery {
struct UndoMetrics final { std::size_t records_considered{};std::size_t committed_transactions_skipped{};std::size_t aborted_transactions{};std::size_t incomplete_transactions{};std::size_t mutations_considered{};std::size_t mutations_undone{};std::size_t mutations_skipped{};std::size_t pages_touched{};std::size_t allocations_released{};std::chrono::nanoseconds duration{}; };
class UndoEngine final { public: [[nodiscard]] static UndoMetrics run(const RecoveryState&,ReplayContext&); };
}
