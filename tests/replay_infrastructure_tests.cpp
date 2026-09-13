#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "ddb/recovery/replay_context.h"
#include "ddb/storage/log_manager.h"
#include "ddb/storage/physical_mutation.h"

namespace { using namespace ddb;
std::filesystem::path path(){auto file=std::filesystem::temp_directory_path()/"ddb_replay_infrastructure.db";std::error_code error;std::filesystem::remove(file,error);std::filesystem::remove(file.string()+".wal",error);return file;}
}
int main(){try{const auto file=path();storage::PageManager pages(file);storage::LogManager log(file.string()+".wal");buffer::BufferPoolManager pool(pages,2,&log);const auto existing=pages.allocate_page();const auto target=storage::PageId(pages.page_count());const auto records=log.records().size();{recovery::ReplayContext replay(pages,pool);if(!replay.active())throw std::runtime_error("replay mode inactive");if(!replay.redo_page_allocate(target)||!pages.is_page_allocated(target)||pages.page_count()!=target.value()+1)throw std::runtime_error("replay materialization allocation state");try{(void)replay.redo_page_allocate(storage::PageId{});throw std::runtime_error("invalid replay id accepted");}catch(const storage::StorageError&){}try{(void)replay.redo_page_allocate(storage::PageId(target.value()+2));throw std::runtime_error("replay gap accepted");}catch(const storage::StorageError&){}if(replay.redo_page_allocate(existing))throw std::runtime_error("existing replay page was materialized");if(log.records().size()!=records)throw std::runtime_error("replay emitted WAL");}auto* page=pool.fetch_page(existing);if(!page)throw std::runtime_error("normal fetch");storage::MutationContext context(pool);context.watch(existing);page->data()[0]=std::byte{7};context.finish(existing,*page);if(!pool.unpin_page(existing,true)||pool.pin_count(existing).value_or(99)!=0||!pool.is_dirty(existing).value_or(false)||!pool.validate_invariants())throw std::runtime_error("normal mutation path changed");pages.flush();std::ifstream raw(file,std::ios::binary);raw.seekg(static_cast<std::streamoff>((target.value()+1)*storage::kPageSize));char magic[4]{};raw.read(magic,4);if(std::string(magic,4)!="DDBP")throw std::runtime_error("materialized page format");std::error_code error;std::filesystem::remove(file,error);std::filesystem::remove(file.string()+".wal",error);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}std::cout<<"All replay infrastructure tests passed\n";}
