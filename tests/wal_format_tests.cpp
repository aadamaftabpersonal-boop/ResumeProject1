#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "ddb/storage/log_manager.h"

namespace {
int failures{};
#define EXPECT(x) do { if (!(x)) { ++failures; std::cerr << __FUNCTION__ << ": " #x "\n"; } } while (false)
using namespace ddb;
std::filesystem::path path() { auto p = std::filesystem::temp_directory_path() / "ddb_wal_format_tests.wal"; std::error_code e; std::filesystem::remove(p, e); return p; }
void format_and_lsn() {
  const auto file = path();
  std::uint64_t last{};
  { storage::LogManager log(file); EXPECT(log.next_lsn() == 1); const auto begin = log.append_begin(7); storage::PhysicalMutation mutation{storage::PageId(4), storage::MutationKind::PayloadWrite, 2, std::vector<std::byte>(storage::kPagePayloadSize), std::vector<std::byte>(storage::kPagePayloadSize)}; mutation.after_image[0] = std::byte{9}; const auto physical = log.append_physical_mutation(7, mutation); const auto commit = log.append_commit(7); EXPECT(begin == 1 && physical == 2 && commit == 3); EXPECT(log.durable_lsn() == 0); log.flush(); EXPECT(log.durable_lsn() == 3); const auto records = log.records(); EXPECT(records.size() == 3 && records[1].type == storage::WalRecordType::PhysicalMutation); const auto decoded = storage::LogManager::decode_physical_mutation(records[1].payload); EXPECT(decoded.page_id == mutation.page_id && decoded.after_image == mutation.after_image); last = commit; }
  { std::ofstream tail(file, std::ios::binary | std::ios::app); const char partial[] = {'D', 'W', 'A'}; tail.write(partial, sizeof partial); }
  { storage::LogManager log(file); EXPECT(log.next_lsn() == last + 1); EXPECT(log.append_abort(8) == last + 1); EXPECT(log.records().size() == 4); }
  std::error_code e; std::filesystem::remove(file, e);
}
void rejects_corruption() {
  storage::WalRecord record{1, 1, storage::WalRecordType::Begin, {}}; auto encoded = storage::LogManager::encode(record); encoded[0] = std::byte{0}; try { (void)storage::LogManager::decode(encoded); EXPECT(false); } catch (const storage::WalError&) { EXPECT(true); }
  encoded = storage::LogManager::encode(record); encoded.back() = std::byte{0}; try { (void)storage::LogManager::decode(encoded); EXPECT(false); } catch (const storage::WalError&) { EXPECT(true); }
  encoded = storage::LogManager::encode(record); encoded.resize(8); try { (void)storage::LogManager::decode(encoded); EXPECT(false); } catch (const storage::WalError&) { EXPECT(true); }
}
}
int main() { try { format_and_lsn(); rejects_corruption(); } catch (const std::exception& e) { ++failures; std::cerr << e.what() << '\n'; } if (failures) return 1; std::cout << "All WAL format tests passed\n"; }
