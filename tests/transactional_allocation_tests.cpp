#include <filesystem>
#include <iostream>
#include "ddb/buffer/buffer_pool_manager.h"
#include "ddb/storage/log_manager.h"
#include "ddb/storage/page_manager.h"
using namespace ddb::storage;
int main(){auto p=std::filesystem::temp_directory_path()/"ddb_tx_alloc.db";std::error_code e;std::filesystem::remove(p,e);std::filesystem::remove(p.string()+".wal",e);try{{PageManager m(p);LogManager l(p.string()+".wal");auto r=m.reserve_page();if(m.is_page_allocated(r)||m.page_count()!=1)throw std::runtime_error("reservation");ddb::buffer::BufferPoolManager b(m,2);try{(void)b.fetch_page(r);throw std::runtime_error("reserved fetch");}catch(const StorageError){}auto a=m.allocate_transactional_page(l,42);if(a!=PageId(1)||!m.is_page_allocated(a)||l.durable_lsn()==0)throw std::runtime_error("transactional allocation");}{PageManager m(p);if(!m.is_page_allocated(PageId(1))||m.page_count()!=2)throw std::runtime_error("persistence");}std::filesystem::remove(p,e);std::filesystem::remove(p.string()+".wal",e);std::cout<<"All transactional allocation tests passed\n";}catch(const std::exception&x){std::cerr<<x.what()<<'\n';return 1;}}
