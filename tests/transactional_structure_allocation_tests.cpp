#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>

#include "ddb/buffer/buffer_pool_manager.h"
#include "ddb/concurrency/lock_manager.h"
#include "ddb/concurrency/transaction_coordinator.h"
#include "ddb/concurrency/transactional_access.h"
#include "ddb/execution/table_heap.h"
#include "ddb/index/bplus_tree.h"
#include "ddb/storage/log_manager.h"

namespace {
using namespace ddb;

std::filesystem::path path(const char* name) {
  const auto file = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove(file, error);
  std::filesystem::remove(file.string() + ".wal", error);
  return file;
}

void require_allocation_precedes_mutation(const std::vector<storage::WalRecord>& records,
                                          concurrency::TransactionId transaction_id) {
  std::set<std::uint64_t> allocations;
  for (const auto& record : records) {
    if (record.transaction_id != transaction_id) continue;
    if (record.type == storage::WalRecordType::PageAllocate) {
      const auto page = storage::LogManager::decode_page_allocate(record.payload);
      allocations.insert(page.value());
      continue;
    }
    if (record.type == storage::WalRecordType::PhysicalMutation) {
      const auto mutation = storage::LogManager::decode_physical_mutation(record.payload);
      if (allocations.contains(mutation.page_id.value())) continue;
    }
  }
  if (allocations.empty()) throw std::runtime_error("expected transactional page allocation");
  for (const auto page_value : allocations) {
    std::uint64_t allocation_lsn{};
    std::uint64_t first_mutation_lsn{};
    for (const auto& record : records) {
      if (record.transaction_id != transaction_id) continue;
      if (record.type == storage::WalRecordType::PageAllocate &&
          storage::LogManager::decode_page_allocate(record.payload).value() == page_value) allocation_lsn = record.lsn;
      if (record.type == storage::WalRecordType::PhysicalMutation &&
          storage::LogManager::decode_physical_mutation(record.payload).page_id.value() == page_value &&
          first_mutation_lsn == 0) first_mutation_lsn = record.lsn;
    }
    if (allocation_lsn == 0 || first_mutation_lsn == 0 || allocation_lsn >= first_mutation_lsn)
      throw std::runtime_error("PAGE_ALLOCATE did not precede the first page mutation");
  }
  for (std::size_t index = 1; index < records.size(); ++index)
    if (records[index - 1].lsn >= records[index].lsn) throw std::runtime_error("WAL LSNs are not strictly increasing");
}

void bplus_splits_and_roots() {
  const auto file = path("ddb_tx_structure_tree.db");
  storage::PageManager pages(file); storage::LogManager log(file.string() + ".wal");
  buffer::BufferPoolManager pool(pages, 32, &log);
  const auto metadata = index::BPlusTree::create(pages, pool, {2, 2});
  index::BPlusTree tree(pages, pool, metadata);
  concurrency::TransactionManager manager(&log); concurrency::LockManager locks;
  concurrency::TransactionCoordinator coordinator(locks, &log);
  concurrency::TransactionalBPlusTree transactional(tree, locks);
  auto transaction = manager.begin();
  for (std::int64_t key = 1; key <= 20; ++key)
    if (!transactional.insert(transaction, key, {storage::PageId(static_cast<std::uint64_t>(key)), 0})) throw std::runtime_error("tree insert failed");
  const auto records = log.records();
  require_allocation_precedes_mutation(records, transaction.id());
  coordinator.commit(transaction);
  if (!tree.validate_invariants()) throw std::runtime_error("tree invariant failure");
  for (const auto& record : records) if (record.transaction_id == transaction.id() && record.type == storage::WalRecordType::PageAllocate) {
    const auto page = storage::LogManager::decode_page_allocate(record.payload);
    if (!pages.is_page_allocated(page) || pool.fetch_page(page) == nullptr) throw std::runtime_error("allocated tree page is inaccessible");
    if (!pool.unpin_page(page, false)) throw std::runtime_error("unpin allocated tree page");
  }
  std::error_code error; std::filesystem::remove(file, error); std::filesystem::remove(file.string() + ".wal", error);
}

void table_growth() {
  const auto file = path("ddb_tx_structure_table.db");
  storage::PageManager pages(file); storage::LogManager log(file.string() + ".wal");
  buffer::BufferPoolManager pool(pages, 32, &log);
  const execution::Schema schema({{"payload", execution::ValueType::String}});
  const auto metadata = execution::TableHeap::create(pages, pool, schema);
  execution::TableHeap table(pages, pool, metadata, schema);
  concurrency::TransactionManager manager(&log); concurrency::LockManager locks;
  concurrency::TransactionCoordinator coordinator(locks, &log);
  concurrency::TransactionalTableHeap transactional(table, locks);
  auto transaction = manager.begin();
  for (int row = 0; row < 4; ++row)
    if (!transactional.insert(transaction, execution::Tuple{{std::string(2000, static_cast<char>('a' + row))}})) throw std::runtime_error("table insert failed");
  const auto records = log.records();
  require_allocation_precedes_mutation(records, transaction.id());
  coordinator.commit(transaction);
  if (!table.validate_invariants() || table.scan().size() != 4) throw std::runtime_error("table invariant failure");
  std::error_code error; std::filesystem::remove(file, error); std::filesystem::remove(file.string() + ".wal", error);
}

void allocation_flush_failure_leaves_page_free() {
  const auto file = path("ddb_tx_structure_failure.db");
  storage::PageManager pages(file); storage::LogManager log(file.string() + ".wal");
  buffer::BufferPoolManager pool(pages, 16, &log);
  const auto metadata = index::BPlusTree::create(pages, pool, {2, 2});
  index::BPlusTree tree(pages, pool, metadata);
  if (!tree.insert(1, {storage::PageId(1), 0}) || !tree.insert(2, {storage::PageId(2), 0})) throw std::runtime_error("tree setup failed");
  concurrency::TransactionManager manager(&log); concurrency::LockManager locks;
  concurrency::TransactionalBPlusTree transactional(tree, locks);
  const auto reserved = storage::PageId(pages.page_count());
  log.fail_next_flush_for_testing();
  auto transaction = manager.begin();
  try { (void)transactional.insert(transaction, 3, {storage::PageId(3), 0}); throw std::runtime_error("forced flush did not fail"); }
  catch (const storage::WalError&) {}
  if (pages.page_count() != reserved.value() + 1 || pages.is_page_allocated(reserved)) throw std::runtime_error("failed allocation activated page");
  try { (void)pool.fetch_page(reserved); throw std::runtime_error("failed allocation was fetchable"); }
  catch (const storage::StorageError&) {}
  std::error_code error; std::filesystem::remove(file, error); std::filesystem::remove(file.string() + ".wal", error);

  const auto table_file = path("ddb_tx_structure_table_failure.db");
  storage::PageManager table_pages(table_file); storage::LogManager table_log(table_file.string() + ".wal");
  buffer::BufferPoolManager table_pool(table_pages, 16, &table_log);
  const execution::Schema schema({{"value", execution::ValueType::Int64}});
  const auto table_metadata = execution::TableHeap::create(table_pages, table_pool, schema);
  execution::TableHeap table(table_pages, table_pool, table_metadata, schema);
  concurrency::TransactionManager table_manager(&table_log); concurrency::LockManager table_locks;
  concurrency::TransactionalTableHeap transactional_table(table, table_locks);
  const auto table_reserved = storage::PageId(table_pages.page_count());
  table_log.fail_next_flush_for_testing();
  auto table_transaction = table_manager.begin();
  try { (void)transactional_table.insert(table_transaction, execution::Tuple{{std::int64_t{7}}}); throw std::runtime_error("forced table flush did not fail"); }
  catch (const storage::WalError&) {}
  if (table_pages.page_count() != table_reserved.value() + 1 || table_pages.is_page_allocated(table_reserved)) throw std::runtime_error("failed table allocation activated page");
  try { (void)table_pool.fetch_page(table_reserved); throw std::runtime_error("failed table allocation was fetchable"); }
  catch (const storage::StorageError&) {}
  std::filesystem::remove(table_file, error); std::filesystem::remove(table_file.string() + ".wal", error);
}
}

int main() {
  try { bplus_splits_and_roots(); table_growth(); allocation_flush_failure_leaves_page_free(); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << "All transactional structure allocation tests passed\n";
}
