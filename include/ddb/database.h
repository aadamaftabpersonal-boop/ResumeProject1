#pragma once

#include <filesystem>
#include <memory>
#include "ddb/buffer/buffer_pool_manager.h"
#include "ddb/concurrency/transaction.h"

namespace ddb {

// Future WAL/recovery owns this lifecycle seam.  It is intentionally a no-op
// today: no log is opened or recovery is performed in this prerequisite.
class DatabaseLifecycle {
 public:
  virtual ~DatabaseLifecycle() = default;
  virtual void open_wal(const std::filesystem::path&) {}
  virtual void recover(storage::PageManager&) {}
  [[nodiscard]] virtual storage::WalDurabilityProvider* wal_durability_provider() noexcept { return nullptr; }
};

class Database final {
 public:
  Database(const std::filesystem::path&, std::size_t buffer_pool_capacity, DatabaseLifecycle* = nullptr);
  [[nodiscard]] storage::PageManager& page_manager() noexcept { return *page_manager_; }
  [[nodiscard]] buffer::BufferPoolManager& buffer_pool() noexcept { return *buffer_pool_; }
  [[nodiscard]] concurrency::TransactionManager& transaction_manager() noexcept { return transaction_manager_; }
 private:
  std::unique_ptr<storage::PageManager> page_manager_;
  std::unique_ptr<buffer::BufferPoolManager> buffer_pool_;
  concurrency::TransactionManager transaction_manager_;
};

}  // namespace ddb
