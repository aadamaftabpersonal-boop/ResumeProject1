#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "ddb/concurrency/transaction_coordinator.h"
#include "ddb/execution/table_heap.h"
#include "ddb/index/bplus_tree.h"
#include "ddb/storage/log_manager.h"

namespace { using namespace ddb;
std::filesystem::path path(const char* name){auto p=std::filesystem::temp_directory_path()/name;std::error_code e;std::filesystem::remove(p,e);std::filesystem::remove(p.string()+".wal",e);return p;}
index::RecordId value(int n){return {storage::PageId(static_cast<std::uint64_t>(n+100)),static_cast<std::uint32_t>(n)};}
void tree_split_abort(){const auto file=path("ddb_abort_tree.db");storage::PageManager pages(file);storage::LogManager log(file.string()+".wal");buffer::BufferPoolManager pool(pages,32,&log);const auto meta=index::BPlusTree::create(pages,pool,{3,3});index::BPlusTree tree(pages,pool,meta);if(!tree.insert(1,value(1))||!tree.insert(2,value(2))||!tree.insert(3,value(3)))throw std::runtime_error("tree baseline");pool.flush_all_pages();concurrency::TransactionManager manager(&log);concurrency::LockManager locks;concurrency::TransactionCoordinator coordinator(locks,&log);auto tx=manager.begin();if(!tree.insert(tx,4,value(4)))throw std::runtime_error("tree split mutation");coordinator.abort(tx);index::BPlusTree restored(pages,pool,meta);index::RecordId out;if(restored.get_value(4,out)||!restored.validate_invariants()||restored.scan(1,4).size()!=3)throw std::runtime_error("tree abort did not restore structure");}
void table_growth_abort(){const auto file=path("ddb_abort_table.db");storage::PageManager pages(file);storage::LogManager log(file.string()+".wal");buffer::BufferPoolManager pool(pages,32,&log);const execution::Schema schema({{"v",execution::ValueType::String}});const auto meta=execution::TableHeap::create(pages,pool,schema);execution::TableHeap table(pages,pool,meta,schema);if(!table.insert(execution::Tuple{{std::string(3000,'a')}}))throw std::runtime_error("table baseline");pool.flush_all_pages();concurrency::TransactionManager manager(&log);concurrency::LockManager locks;concurrency::TransactionCoordinator coordinator(locks,&log);auto tx=manager.begin();if(!table.insert(tx,execution::Tuple{{std::string(3000,'b')}}))throw std::runtime_error("table growth mutation");coordinator.abort(tx);execution::TableHeap restored(pages,pool,meta,schema);if(!restored.validate_invariants()||restored.scan().size()!=1)throw std::runtime_error("table abort did not restore structure");}
}
int main(){try{tree_split_abort();table_growth_abort();}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"All physical abort structure tests passed\n";}
