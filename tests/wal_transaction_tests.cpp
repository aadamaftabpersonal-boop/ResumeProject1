#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ddb/concurrency/transaction_coordinator.h"
#include "ddb/storage/log_manager.h"

namespace {
int failures{};
#define EXPECT(x) do { if (!(x)) { ++failures; std::cerr << __FUNCTION__ << ": " #x "\n"; } } while (false)
using namespace ddb;
std::filesystem::path path() { auto p = std::filesystem::temp_directory_path() / "ddb_wal_transaction_tests.db"; std::error_code e; std::filesystem::remove(p, e); std::filesystem::remove(p.string() + ".wal", e); return p; }
void lifecycle_and_durability() {
  const auto file = path(); storage::PageManager pages(file); storage::LogManager log(file.string() + ".wal"); buffer::BufferPoolManager pool(pages, 2, &log); concurrency::TransactionManager manager(&log); concurrency::LockManager locks; concurrency::TransactionCoordinator coordinator(locks, &log);
  const auto first = pages.allocate_page(); const auto second = pages.allocate_page(); auto tx = manager.begin(); auto& context = tx.mutation_context(pool);
  for (const auto [id, value] : std::vector<std::pair<storage::PageId, unsigned>>{{first, 1}, {second, 2}}) { context.watch(id); auto* page = pool.fetch_page(id); page->data()[0] = std::byte{static_cast<unsigned char>(value)}; context.finish(id, *page); EXPECT(pool.unpin_page(id, true)); }
  const auto& mutations = tx.pending_mutations(); EXPECT(mutations.size() == 2); EXPECT(pool.fetch_page(first)->lsn() == 2); EXPECT(pool.unpin_page(first, false));
  try { (void)pool.flush_page(first); EXPECT(false); } catch (const storage::WalDurabilityError&) { EXPECT(true); }
  coordinator.commit(tx); EXPECT(tx.state() == concurrency::TransactionState::Committed); EXPECT(log.durable_lsn() == 4); EXPECT(pool.flush_page(first));
  const auto records = log.records(); EXPECT(records.size() == 4); EXPECT(records[0].type == storage::WalRecordType::Begin && records[1].type == storage::WalRecordType::PhysicalMutation && records[2].type == storage::WalRecordType::PhysicalMutation && records[3].type == storage::WalRecordType::Commit); EXPECT(records[1].lsn == 2 && records[2].lsn == 3);
  std::error_code e; std::filesystem::remove(file, e); std::filesystem::remove(file.string() + ".wal", e);
}
void physical_abort_rolls_back_before_terminal_record() {
  const auto file=path();storage::PageManager pages(file);storage::LogManager log(file.string()+".wal");buffer::BufferPoolManager pool(pages,2,&log);concurrency::TransactionManager manager(&log);concurrency::LockManager locks;concurrency::TransactionCoordinator coordinator(locks,&log);const auto page=pages.allocate_page();auto tx=manager.begin();auto& context=tx.mutation_context(pool);
  context.watch(page);auto* raw=pool.fetch_page(page);raw->data()[0]=std::byte{7};context.finish(page,*raw);EXPECT(pool.unpin_page(page,true));
  coordinator.abort(tx);EXPECT(tx.state()==concurrency::TransactionState::Aborted);EXPECT(!tx.has_unresolved_mutations_for_abort());raw=pool.fetch_page(page);EXPECT(raw->data()[0]==std::byte{0});EXPECT(raw->lsn()==0);EXPECT(pool.unpin_page(page,false));const auto records=log.records();EXPECT(records.back().type==storage::WalRecordType::Abort);EXPECT(log.durable_lsn()==records.back().lsn);EXPECT(pool.flush_page(page));
  std::error_code e;std::filesystem::remove(file,e);std::filesystem::remove(file.string()+".wal",e);
}
}
int main() { try { lifecycle_and_durability(); physical_abort_rolls_back_before_terminal_record(); } catch (const std::exception& e) { ++failures; std::cerr << e.what() << '\n'; } if (failures) return 1; std::cout << "All WAL transaction tests passed\n"; }
