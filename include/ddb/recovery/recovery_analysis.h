#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

#include "ddb/storage/log_manager.h"

namespace ddb::recovery {

enum class TransactionStatus : std::uint8_t { Committed, Aborted, Incomplete };

struct AllocationRecord final { std::uint64_t lsn; std::uint64_t transaction_id; ddb::storage::PageId page_id; };
struct MutationRecord final { std::uint64_t lsn; std::uint64_t transaction_id; ddb::storage::PhysicalMutation mutation; };
struct TransactionAnalysis final {
  std::uint64_t transaction_id{};
  TransactionStatus status{TransactionStatus::Incomplete};
  bool began{false};
  bool committed{false};
  bool aborted{false};
  std::vector<std::size_t> allocation_indexes;
  std::vector<std::size_t> mutation_indexes;
};
struct RecoveryMetrics final {
  std::uintmax_t wal_bytes_scanned{};
  std::size_t total_records{};
  std::size_t transactions_observed{};
  std::size_t committed_transactions{};
  std::size_t aborted_transactions{};
  std::size_t incomplete_transactions{};
  std::size_t page_allocates{};
  std::size_t physical_mutations{};
  std::uint64_t first_lsn{};
  std::uint64_t last_lsn{};
  std::chrono::nanoseconds scan_duration{};
};
struct RecoveryState final {
  std::vector<ddb::storage::WalRecord> records;
  std::vector<AllocationRecord> allocations;
  std::vector<MutationRecord> mutations;
  std::map<std::uint64_t, TransactionAnalysis> transactions;
  RecoveryMetrics metrics;
  [[nodiscard]] const TransactionAnalysis* transaction(std::uint64_t) const noexcept;
  [[nodiscard]] std::vector<const AllocationRecord*> allocations_for_page(ddb::storage::PageId) const;
  [[nodiscard]] std::vector<const MutationRecord*> mutations_for_page(ddb::storage::PageId) const;
};

class RecoveryAnalyzer final {
 public:
  [[nodiscard]] static RecoveryState analyze(const std::filesystem::path& wal_path);
  [[nodiscard]] static RecoveryState analyze(const ddb::storage::LogManager&);
};
}  // namespace ddb::recovery
