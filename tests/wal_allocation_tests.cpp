#include <filesystem>
#include <fstream>
#include <iostream>
#include "ddb/storage/log_manager.h"
using namespace ddb::storage;
int main(){auto p=std::filesystem::temp_directory_path()/"ddb_wal_allocation_tests.wal";std::error_code e;std::filesystem::remove(p,e);try{{LogManager l(p);auto a=l.append_page_allocate(42,PageId(7));if(a!=1)throw std::runtime_error("LSN");l.flush();auto r=l.records();if(r.size()!=1||r[0].type!=WalRecordType::PageAllocate||r[0].transaction_id!=42||LogManager::decode_page_allocate(r[0].payload)!=PageId(7))throw std::runtime_error("round trip");try{(void)LogManager::decode_page_allocate({});throw std::runtime_error("length");}catch(const WalError){}}{LogManager l(p);if(l.next_lsn()!=2)throw std::runtime_error("persistence");}std::filesystem::remove(p,e);std::cout<<"All WAL allocation tests passed\n";}catch(const std::exception& x){std::cerr<<x.what()<<'\n';return 1;}}
