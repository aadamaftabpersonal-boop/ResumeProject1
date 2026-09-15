#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "ddb/recovery/redo_engine.h"
#include "ddb/recovery/undo_engine.h"
namespace { using namespace ddb;
std::filesystem::path path(){auto p=std::filesystem::temp_directory_path()/"ddb_recovery_interleaving.db";std::error_code e;std::filesystem::remove(p,e);std::filesystem::remove(p.string()+".wal",e);return p;}
std::vector<char> bytes(const std::filesystem::path&p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
storage::PhysicalMutation mutation(storage::PageId p,unsigned char b0,unsigned char b1,unsigned char a0,unsigned char a1){std::vector<std::byte>b(storage::kPagePayloadSize),a(storage::kPagePayloadSize);b[0]=std::byte{b0};b[1]=std::byte{b1};a[0]=std::byte{a0};a[1]=std::byte{a1};return {p,storage::MutationKind::PayloadWrite,0,std::move(b),std::move(a)};}
void regression(){const auto db=path();storage::PageManager pages(db);storage::LogManager log(db.string()+".wal");buffer::BufferPoolManager pool(pages,4,&log);const auto page=pages.allocate_page();(void)log.append_begin(2);(void)log.append_physical_mutation(2,mutation(page,0,0,1,0));(void)log.append_begin(1);const auto winner=log.append_physical_mutation(1,mutation(page,1,0,1,9));(void)log.append_commit(1);const auto loser=log.append_physical_mutation(2,mutation(page,1,9,2,9));log.flush();const auto wal_before=bytes(log.path());auto*raw=pool.fetch_page(page);raw->data()[0]=std::byte{2};raw->data()[1]=std::byte{9};raw->set_lsn(loser);if(!pool.unpin_page(page,true))throw std::runtime_error("unpin crash page");const auto state=recovery::RecoveryAnalyzer::analyze(log);recovery::ReplayContext replay(pages,pool);(void)recovery::RedoEngine::run(state,replay);const auto undo=recovery::UndoEngine::run(state,replay);raw=pool.fetch_page(page);if(!raw||raw->data()[0]!=std::byte{0}||raw->data()[1]!=std::byte{9}||raw->lsn()!=winner)throw std::runtime_error("same-page winner was not preserved");if(!pool.unpin_page(page,false))throw std::runtime_error("unpin result");if(undo.mutations_undone!=2||bytes(log.path())!=wal_before||!pool.validate_invariants())throw std::runtime_error("interleaving metrics, WAL, or pool invariant");if(recovery::UndoEngine::run(state,replay).mutations_undone!=0)throw std::runtime_error("same-context reconstruction is not idempotent");}
}
int main(){try{regression();}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"All recovery interleaving tests passed\n";}
