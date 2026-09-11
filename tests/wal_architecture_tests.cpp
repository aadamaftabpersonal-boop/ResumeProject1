#include <filesystem>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ddb/database.h"
#include "ddb/concurrency/transaction_coordinator.h"
#include "ddb/concurrency/transactional_access.h"

namespace {
int failures = 0;
#define EXPECT(x) do { if (!(x)) { ++failures; std::cerr << __FUNCTION__ << ": " #x "\n"; } } while (false)
using namespace ddb;

std::filesystem::path path(const char* name) {
  const auto file = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove(file, error);
  std::filesystem::remove(file.string() + ".wal", error);
  return file;
}

struct FakeWal final : storage::WalDurabilityProvider {
  std::uint64_t durable{};
  [[nodiscard]] std::uint64_t durable_lsn() const noexcept override { return durable; }
};

struct AssigningFinalizer final : storage::MutationFinalizer {
  std::vector<storage::PhysicalMutation> mutations;
  [[nodiscard]] std::optional<std::uint64_t> finalize_mutation(storage::PhysicalMutation mutation) override {
    mutations.push_back(std::move(mutation));
    return 42;
  }
};

void durability_gate_all_write_paths() {
  const auto file = path("ddb_wal_architecture_gate.db");
  storage::PageManager disk(file);
  const auto first = disk.allocate_page();
  const auto second = disk.allocate_page();
  FakeWal wal;
  {
    buffer::BufferPoolManager pool(disk, 1, &wal);
    auto* page = pool.fetch_page(first);
    page->data()[0] = std::byte{0x7f};
    page->set_lsn(9);
    EXPECT(pool.unpin_page(first, true));
    try { (void)pool.flush_page(first); EXPECT(false); } catch (const storage::WalDurabilityError&) { EXPECT(true); }
    try { pool.flush_all_pages(); EXPECT(false); } catch (const storage::WalDurabilityError&) { EXPECT(true); }
    try { (void)pool.fetch_page(second); EXPECT(false); } catch (const storage::WalDurabilityError&) { EXPECT(true); }
    EXPECT(pool.contains(first));
    wal.durable = 9;
    EXPECT(pool.flush_page(first));
    EXPECT(pool.fetch_page(second) != nullptr);
    EXPECT(pool.unpin_page(second, false));
  }
  std::error_code error;
  std::filesystem::remove(file, error);
}

void shutdown_gate_and_page_lsn_lifecycle() {
  const auto file = path("ddb_wal_architecture_lsn.db");
  storage::PageManager disk(file);
  const auto id = disk.allocate_page();
  FakeWal wal;
  {
    buffer::BufferPoolManager pool(disk, 2, &wal);
    AssigningFinalizer finalizer;
    storage::MutationContext context(pool, &finalizer);
    auto* page = pool.fetch_page(id);
    context.watch(id);
    page->data()[0] = std::byte{0x2a};
    context.finish(id, *page);
    EXPECT(finalizer.mutations.size() == 1);
    EXPECT(page->lsn() == 42);  // assigned while the caller still owns the pin
    EXPECT(pool.unpin_page(id, true));
    try { (void)pool.flush_page(id); EXPECT(false); } catch (const storage::WalDurabilityError&) { EXPECT(true); }
    wal.durable = 42;
    EXPECT(pool.flush_page(id));
  }
  {
    FakeWal blocked;
    buffer::BufferPoolManager pool(disk, 1, &blocked);
    auto* page = pool.fetch_page(id);
    page->data()[1] = std::byte{0x55};
    page->set_lsn(99);
    EXPECT(pool.unpin_page(id, true));
  }  // destructor attempts flush, observes the gate, and writes nothing
  storage::Page page;
  disk.read_page(id, page);
  EXPECT(page.data()[1] == std::byte{0});
  std::error_code error;
  std::filesystem::remove(file, error);
}

execution::Schema schema() { return execution::Schema({{"value", execution::ValueType::Int64}}); }
execution::Tuple tuple(std::int64_t value) { return execution::Tuple{{value}}; }

void undo_payload(buffer::BufferPoolManager& pool, const storage::PhysicalMutation& mutation) {
  auto* page = pool.fetch_page(mutation.page_id);
  if (page == nullptr) throw std::runtime_error("cannot pin undo page");
  std::memcpy(page->data(), mutation.before_image.data(), mutation.before_image.size());
  if (!pool.unpin_page(mutation.page_id, true)) throw std::runtime_error("cannot unpin undo page");
}

void pending_undo_resolution() {
  const auto file = path("ddb_pending_undo_resolution.db");
  storage::PageManager disk(file);
  FakeWal wal;
  buffer::BufferPoolManager pool(disk, 4, &wal);
  const auto page_a = disk.allocate_page();
  const auto page_b = disk.allocate_page();
  const auto page_c = disk.allocate_page();
  concurrency::TransactionManager manager;

  // PageLSN 0 is a valid restored state. A zero LSN remains invalid for the
  // normal forward WAL-finalization API.
  auto zero_tx = manager.begin();
  auto& zero_context = zero_tx.mutation_context(pool);
  zero_context.watch(page_a);
  auto* page = pool.fetch_page(page_a);
  page->data()[0] = std::byte{1};
  zero_context.finish(page_a, *page);
  EXPECT(pool.unpin_page(page_a, true));
  try { zero_tx.finalize_next_mutation(0); EXPECT(false); } catch (const std::invalid_argument&) { EXPECT(true); }
  const auto& zero_mutations = zero_tx.pending_mutations();
  undo_payload(pool, zero_mutations[0]);
  zero_tx.resolve_mutation_for_undo(0, 0);
  page = pool.fetch_page(page_a);
  EXPECT(page->lsn() == 0 && page->data()[0] == std::byte{0});
  EXPECT(pool.unpin_page(page_a, false));
  EXPECT(pool.pending_mutation_count(page_a) == 0);
  EXPECT(pool.flush_page(page_a));

  // A previously WAL-finalized mutation has no pending gate, but undo still
  // restores PageLSN zero through the separate replay-only path.
  auto finalized_tx = manager.begin();
  auto& finalized = finalized_tx.mutation_context(pool);
  finalized.watch(page_c);
  page = pool.fetch_page(page_c);
  page->data()[0] = std::byte{9};
  finalized.finish(page_c, *page);
  EXPECT(pool.unpin_page(page_c, true));
  finalized_tx.finalize_next_mutation(17);
  undo_payload(pool, finalized_tx.pending_mutations()[0]);
  finalized_tx.resolve_mutation_for_undo(0, 0);
  page = pool.fetch_page(page_c);
  EXPECT(page->lsn() == 0 && page->data()[0] == std::byte{0});
  EXPECT(pool.unpin_page(page_c, false));
  EXPECT(pool.flush_page(page_c));

  // Same-page history, not global adjacency, supplies the restored PageLSN.
  auto history_tx = manager.begin();
  auto& history = history_tx.mutation_context(pool);
  for (const auto [id, value] : std::vector<std::pair<storage::PageId, unsigned>>{{page_a, 2}, {page_b, 3}, {page_a, 4}}) {
    history.watch(id);
    page = pool.fetch_page(id);
    page->data()[0] = std::byte{static_cast<unsigned char>(value)};
    history.finish(id, *page);
    EXPECT(pool.unpin_page(id, true));
  }
  history_tx.finalize_next_mutation(10);  // A
  history_tx.finalize_next_mutation(11);  // B (unrelated)
  history_tx.finalize_next_mutation(12);  // A
  wal.durable = 100;
  const auto& history_mutations = history_tx.pending_mutations();
  undo_payload(pool, history_mutations[2]);
  history_tx.resolve_mutation_for_undo(2, 10);
  page = pool.fetch_page(page_a);
  EXPECT(page->lsn() == 10 && page->data()[0] == std::byte{2});
  EXPECT(pool.unpin_page(page_a, false));

  // Unfinalized multi-page mutations resolve in reverse physical order and
  // release their independent pending gates.
  auto reverse_tx = manager.begin();
  auto& reverse = reverse_tx.mutation_context(pool);
  for (const auto [id, value] : std::vector<std::pair<storage::PageId, unsigned>>{{page_a, 5}, {page_b, 6}}) {
    reverse.watch(id);
    page = pool.fetch_page(id);
    page->data()[1] = std::byte{static_cast<unsigned char>(value)};
    reverse.finish(id, *page);
    EXPECT(pool.unpin_page(id, true));
  }
  const auto& reverse_mutations = reverse_tx.pending_mutations();
  try { reverse_tx.resolve_mutation_for_undo(0, 10); EXPECT(false); } catch (const std::logic_error&) { EXPECT(true); }
  undo_payload(pool, reverse_mutations[1]);
  reverse_tx.resolve_mutation_for_undo(1, 11);
  undo_payload(pool, reverse_mutations[0]);
  reverse_tx.resolve_mutation_for_undo(0, 10);
  EXPECT(pool.pending_mutation_count(page_a) == 0 && pool.pending_mutation_count(page_b) == 0);
  page = pool.fetch_page(page_b);
  EXPECT(page->lsn() == 11 && page->data()[1] == std::byte{0});
  EXPECT(pool.unpin_page(page_b, false));
  EXPECT(pool.flush_page(page_a));

  try { pool.resolve_pending_mutation(storage::PageId{}, 0); EXPECT(false); } catch (const std::logic_error&) { EXPECT(true); }
  std::error_code error;
  std::filesystem::remove(file, error);
}

void transaction_owns_and_propagates_mutations() {
  const auto file = path("ddb_wal_architecture_transactions.db");
  storage::PageManager disk(file);
  FakeWal wal;
  buffer::BufferPoolManager pool(disk, 16, &wal);
  const auto metadata = execution::TableHeap::create(disk, pool, schema());
  execution::TableHeap table(disk, pool, metadata, schema());
  concurrency::TransactionManager manager;
  concurrency::LockManager locks;
  concurrency::TransactionalTableHeap transactional_table(table, locks);
  auto table_tx = manager.begin();
  const auto row = transactional_table.insert(table_tx, tuple(7));
  EXPECT(row.has_value());
  EXPECT(transactional_table.erase(table_tx, *row));
  const auto& table_mutations = table_tx.pending_mutations();
  EXPECT(table_mutations.size() == 4);
  for (std::size_t index = 0; index < table_mutations.size(); ++index) EXPECT(table_mutations[index].sequence == index);
  EXPECT(pool.pending_mutation_count(table_mutations.front().page_id).value_or(0) != 0);
  try { pool.flush_all_pages(); EXPECT(false); } catch (const std::logic_error&) { EXPECT(true); }

  struct FinalizingParticipant final : concurrency::TransactionDurabilityParticipant {
    std::uint64_t next_lsn{100};
    void finalize(concurrency::Transaction& tx) {
      while (tx.has_unfinalized_mutations()) tx.finalize_next_mutation(next_lsn++);
    }
    void prepare_commit(concurrency::Transaction& tx) override { finalize(tx); }
    void prepare_abort(concurrency::Transaction& tx) override { finalize(tx); }
  } finalizer;
  concurrency::TransactionCoordinator coordinator(locks, &finalizer);
  coordinator.commit(table_tx);
  wal.durable = 1000;
  const auto tree_metadata = index::BPlusTree::create(disk, pool, {3, 3});
  index::BPlusTree tree(disk, pool, tree_metadata);
  concurrency::TransactionalBPlusTree transactional_tree(tree, locks);
  auto tree_tx = manager.begin();
  EXPECT(transactional_tree.insert(tree_tx, 11, {row->page_id, row->slot_id}));
  EXPECT(transactional_tree.remove(tree_tx, 11));
  const auto& tree_mutations = tree_tx.pending_mutations();
  EXPECT(tree_mutations.size() >= 2);
  for (std::size_t index = 0; index < tree_mutations.size(); ++index) EXPECT(tree_mutations[index].sequence == index);
  coordinator.commit(tree_tx);
  EXPECT(table_tx.state() == concurrency::TransactionState::Committed);
  EXPECT(tree_tx.state() == concurrency::TransactionState::Committed);
  EXPECT(locks.lock_count() == 0);
  std::error_code error;
  std::filesystem::remove(file, error);
}

struct CoordinatorProbe final : concurrency::TransactionDurabilityParticipant {
  std::vector<std::string> calls;
  void prepare_commit(concurrency::Transaction&) override { calls.emplace_back("commit"); }
  void prepare_abort(concurrency::Transaction&) override { calls.emplace_back("abort"); }
};

void coordinator_order_and_database_lifecycle() {
  concurrency::TransactionManager manager;
  concurrency::LockManager locks;
  CoordinatorProbe probe;
  concurrency::TransactionCoordinator coordinator(locks, &probe);
  auto committed = manager.begin();
  auto aborted = manager.begin();
  coordinator.commit(committed);
  coordinator.abort(aborted);
  EXPECT(probe.calls == std::vector<std::string>({"commit", "abort"}));
  EXPECT(committed.state() == concurrency::TransactionState::Committed);
  EXPECT(aborted.state() == concurrency::TransactionState::Aborted);

  struct Lifecycle final : DatabaseLifecycle {
    std::vector<std::string> calls;
    FakeWal wal;
    void open_wal(const std::filesystem::path&) override { calls.emplace_back("open_wal"); }
    void recover(storage::PageManager&) override { calls.emplace_back("recover"); }
    [[nodiscard]] storage::WalDurabilityProvider* wal_durability_provider() noexcept override { return &wal; }
  } lifecycle;
  const auto file = path("ddb_wal_architecture_database.db");
  Database database(file, 2, &lifecycle);
  EXPECT(lifecycle.calls == std::vector<std::string>({"open_wal", "recover"}));
  EXPECT(database.transaction_manager().begin().state() == concurrency::TransactionState::Growing);
  std::error_code error;
  std::filesystem::remove(file, error);
}
}

int main() {
  try {
    durability_gate_all_write_paths();
    shutdown_gate_and_page_lsn_lifecycle();
    pending_undo_resolution();
    transaction_owns_and_propagates_mutations();
    coordinator_order_and_database_lifecycle();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  if (failures != 0) return 1;
  std::cout << "All WAL architecture tests passed\n";
}
