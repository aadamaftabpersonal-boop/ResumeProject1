#pragma once

#include <chrono>
#include <cstddef>

#include "ddb/recovery/recovery_analysis.h"
#include "ddb/recovery/replay_context.h"

namespace ddb::recovery {
struct RedoMetrics final {
  std::size_t records_considered{};
  std::size_t committed_transactions{};
  std::size_t page_allocates_processed{};
  std::size_t pages_materialized{};
  std::size_t physical_mutations_considered{};
  std::size_t mutations_applied{};
  std::size_t mutations_skipped_page_lsn{};
  std::chrono::nanoseconds duration{};
};
class RedoEngine final {
 public:
  [[nodiscard]] static RedoMetrics run(const RecoveryState&, ReplayContext&);
};
}  // namespace ddb::recovery
