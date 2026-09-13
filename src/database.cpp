#include "ddb/database.h"

namespace ddb {

recovery::RecoveryState Database::analyze_wal(const std::filesystem::path& database_path) {
  return recovery::RecoveryAnalyzer::analyze(database_path.string() + ".wal");
}

Database::Database(const std::filesystem::path& path, std::size_t buffer_pool_capacity, DatabaseLifecycle* lifecycle) {
  log_manager_ = std::make_unique<storage::LogManager>(path.string() + ".wal");
  if (lifecycle != nullptr) lifecycle->open_wal(path.string() + ".wal");
  page_manager_ = std::make_unique<storage::PageManager>(path);
  if (lifecycle != nullptr) lifecycle->recover(*page_manager_);
  buffer_pool_ = std::make_unique<buffer::BufferPoolManager>(*page_manager_, buffer_pool_capacity,
      lifecycle == nullptr ? static_cast<storage::WalDurabilityProvider*>(log_manager_.get()) : lifecycle->wal_durability_provider());
  transaction_manager_ = std::make_unique<concurrency::TransactionManager>(log_manager_.get());
}

}  // namespace ddb
