#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "ddb/recovery/recovery_analysis.h"

namespace {
using namespace ddb;
std::filesystem::path path(const char* name) { auto file=std::filesystem::temp_directory_path()/name; std::error_code error; std::filesystem::remove(file,error); return file; }
storage::PhysicalMutation mutation(storage::PageId page, std::uint64_t sequence=0) { return {page,storage::MutationKind::PayloadWrite,sequence,std::vector<std::byte>(storage::kPagePayloadSize),std::vector<std::byte>(storage::kPagePayloadSize,std::byte{1})}; }
void expect_throw(const std::filesystem::path& file) { try { (void)recovery::RecoveryAnalyzer::analyze(file); throw std::runtime_error("corrupt WAL accepted"); } catch(const storage::WalError&) {} }
void patch_byte(const std::filesystem::path& file, std::streamoff offset, unsigned char byte) { std::fstream stream(file,std::ios::binary|std::ios::in|std::ios::out); stream.seekp(offset); stream.put(static_cast<char>(byte)); }

void empty_and_classification() {
  const auto file=path("ddb_recovery_empty.wal"); { storage::LogManager log(file); auto state=recovery::RecoveryAnalyzer::analyze(log); if(state.metrics.total_records||state.metrics.transactions_observed||state.metrics.first_lsn) throw std::runtime_error("empty WAL analysis"); }
  const auto tx_file=path("ddb_recovery_transactions.wal"); { storage::LogManager log(tx_file); (void)log.append_begin(1); (void)log.append_page_allocate(1,storage::PageId(4)); (void)log.append_physical_mutation(1,mutation(storage::PageId(4))); (void)log.append_page_allocate(1,storage::PageId(5)); (void)log.append_physical_mutation(1,mutation(storage::PageId(5),1)); (void)log.append_commit(1); (void)log.append_begin(2); (void)log.append_abort(2); (void)log.append_begin(3); auto state=recovery::RecoveryAnalyzer::analyze(log); if(state.metrics.total_records!=9||state.metrics.committed_transactions!=1||state.metrics.aborted_transactions!=1||state.metrics.incomplete_transactions!=1) throw std::runtime_error("transaction classification"); if(state.transaction(1)->status!=recovery::TransactionStatus::Committed||state.transaction(2)->status!=recovery::TransactionStatus::Aborted||state.transaction(3)->status!=recovery::TransactionStatus::Incomplete) throw std::runtime_error("transaction status"); if(state.allocations.size()!=2||state.mutations.size()!=2||state.allocations_for_page(storage::PageId(4)).front()->transaction_id!=1||state.mutations_for_page(storage::PageId(5)).front()->transaction_id!=1) throw std::runtime_error("allocation mutation relationship"); }
}

void ordering_and_truncated_tail() {
  const auto file=path("ddb_recovery_order.wal"); { storage::LogManager log(file); (void)log.append_begin(10); (void)log.append_begin(20); (void)log.append_physical_mutation(10,mutation(storage::PageId(1),0)); (void)log.append_page_allocate(20,storage::PageId(2)); (void)log.append_physical_mutation(10,mutation(storage::PageId(1),1)); auto state=recovery::RecoveryAnalyzer::analyze(log); if(state.transaction(10)->mutation_indexes.size()!=2||state.mutations[state.transaction(10)->mutation_indexes[0]].lsn>=state.mutations[state.transaction(10)->mutation_indexes[1]].lsn) throw std::runtime_error("transaction-local ordering"); for(std::size_t i=1;i<state.records.size();++i) if(state.records[i-1].lsn>=state.records[i].lsn) throw std::runtime_error("LSN ordering"); }
  { std::ofstream tail(file,std::ios::binary|std::ios::app); const char bytes[]{'D','W','A'}; tail.write(bytes,sizeof bytes); }
  auto state=recovery::RecoveryAnalyzer::analyze(file); if(state.records.size()!=5) throw std::runtime_error("truncated final record");
}

void corrupt_complete_records() {
  struct Corruption { const char* name; std::streamoff offset; unsigned char value; };
  const Corruption corruptions[]={{"magic",0,'X'},{"version",4,2},{"header",6,0},{"total",8,0},{"payload",32,1},{"checksum",36,0},{"lsn",12,0}};
  for(const auto& corruption:corruptions) { const auto file=path(corruption.name); { storage::LogManager log(file); (void)log.append_begin(1); } patch_byte(file,corruption.offset,corruption.value); expect_throw(file); }
  const auto middle=path("ddb_recovery_middle.wal"); { storage::LogManager log(middle); (void)log.append_begin(1); (void)log.append_begin(2); } patch_byte(middle,40,static_cast<unsigned char>('X')); expect_throw(middle);
  const auto non_monotonic=path("ddb_recovery_non_monotonic.wal"); { const auto record=storage::LogManager::encode({1,1,storage::WalRecordType::Begin,{}}); std::ofstream out(non_monotonic,std::ios::binary); out.write(reinterpret_cast<const char*>(record.data()),static_cast<std::streamsize>(record.size())); out.write(reinterpret_cast<const char*>(record.data()),static_cast<std::streamsize>(record.size())); } expect_throw(non_monotonic);
  const auto malformed=path("ddb_recovery_malformed.wal"); { std::ofstream out(malformed,std::ios::binary); const storage::WalRecord record{1,1,storage::WalRecordType::PageAllocate,{}}; const auto bytes=storage::LogManager::encode(record); out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())); } expect_throw(malformed);
}
}
int main(){try{empty_and_classification();ordering_and_truncated_tail();corrupt_complete_records();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}std::cout<<"All recovery analysis tests passed\n";}
