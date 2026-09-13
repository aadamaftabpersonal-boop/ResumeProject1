#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include "ddb/storage/physical_mutation.h"
#include "ddb/storage/wal_durability.h"
#include "ddb/concurrency/transaction_coordinator.h"

namespace ddb::storage {

enum class WalRecordType : std::uint8_t { Begin = 1, PhysicalMutation = 2, Commit = 3, Abort = 4, PageAllocate = 5 };

struct WalRecord final {
  std::uint64_t lsn{};
  std::uint64_t transaction_id{};
  WalRecordType type{};
  std::vector<std::byte> payload;
};

class WalError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

// A synchronous, append-only local WAL. Records are explicitly encoded in
// little-endian form; no C++ object representation reaches disk.
class LogManager final : public WalDurabilityProvider, public ddb::concurrency::TransactionLogSink,
                         public ddb::concurrency::TransactionDurabilityParticipant {
 public:
  explicit LogManager(const std::filesystem::path& path);
  ~LogManager() noexcept;
  LogManager(const LogManager&) = delete;
  LogManager& operator=(const LogManager&) = delete;

  [[nodiscard]] std::uint64_t append_begin(std::uint64_t transaction_id);
  [[nodiscard]] std::uint64_t append_physical_mutation(std::uint64_t transaction_id, const PhysicalMutation&);
  [[nodiscard]] std::uint64_t append_commit(std::uint64_t transaction_id);
  [[nodiscard]] std::uint64_t append_abort(std::uint64_t transaction_id);
  [[nodiscard]] std::uint64_t append_page_allocate(std::uint64_t transaction_id, PageId page_id);
  void log_begin(ddb::concurrency::TransactionId) override;
  [[nodiscard]] std::uint64_t log_physical_mutation(ddb::concurrency::TransactionId, const PhysicalMutation&) override;
  [[nodiscard]] PageId allocate_page(PageManager&, ddb::concurrency::TransactionId) override;
  void prepare_commit(ddb::concurrency::Transaction&) override;
  void prepare_abort(ddb::concurrency::Transaction&) override;
  void flush();
  // Test-only deterministic failure seam for callers that must prove that a
  // failed allocation flush never activates the reserved page.
  void fail_next_flush_for_testing() noexcept { fail_next_flush_ = true; }
  [[nodiscard]] std::uint64_t durable_lsn() const noexcept override;
  [[nodiscard]] std::uint64_t next_lsn() const noexcept;
  [[nodiscard]] std::vector<WalRecord> records() const;
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  [[nodiscard]] static std::vector<std::byte> encode(const WalRecord&);
  [[nodiscard]] static WalRecord decode(const std::vector<std::byte>&);
  [[nodiscard]] static std::vector<std::byte> encode_physical_mutation(const PhysicalMutation&);
  [[nodiscard]] static PhysicalMutation decode_physical_mutation(const std::vector<std::byte>&);
  [[nodiscard]] static std::vector<std::byte> encode_page_allocate(PageId);
  [[nodiscard]] static PageId decode_page_allocate(const std::vector<std::byte>&);

 private:
  [[nodiscard]] std::uint64_t append(WalRecordType, std::uint64_t transaction_id, std::vector<std::byte> payload = {});
  void open_and_scan();
  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::fstream file_;
  std::uint64_t next_lsn_{1};
  std::uint64_t durable_lsn_{0};
  bool fail_next_flush_{false};
};
}  // namespace ddb::storage
