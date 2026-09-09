#include <filesystem>
#include <iostream>
#include "ddb/storage/physical_mutation.h"
namespace {int failures=0;
#define EXPECT(x) do{if(!(x)){++failures;std::cerr<<#x<<"\n";}}while(false)
using namespace ddb;void release(buffer::BufferPoolManager&p,storage::PageId id,bool dirty){if(!p.unpin_page(id,dirty))throw std::runtime_error("unpin");}std::filesystem::path path(){auto p=std::filesystem::temp_directory_path()/"ddb_mutation_test.db";std::error_code e;std::filesystem::remove(p,e);return p;}
void capture_replay(){auto f=path();storage::PageManager disk(f);buffer::BufferPoolManager pool(disk,2);auto a=disk.allocate_page(),b=disk.allocate_page();storage::MutationContext c(pool);c.watch(a);auto*p=pool.fetch_page(a);p->data()[0]=std::byte{1};release(pool,a,true);c.finish(a);c.watch(a);p=pool.fetch_page(a);p->data()[1]=std::byte{2};release(pool,a,true);c.finish(a);c.watch(b);p=pool.fetch_page(b);p->data()[0]=std::byte{3};release(pool,b,true);c.finish(b);auto m=c.finalize();EXPECT(m.size()==3&&m[0].page_id==a&&m[1].page_id==a&&m[2].page_id==b);storage::apply_before_image(pool,m[1]);storage::apply_before_image(pool,m[0]);p=pool.fetch_page(a);EXPECT(p->data()[0]==std::byte{0}&&p->data()[1]==std::byte{0});release(pool,a,false);storage::apply_after_image(pool,m[0]);storage::apply_after_image(pool,m[1]);p=pool.fetch_page(a);EXPECT(p->data()[0]==std::byte{1}&&p->data()[1]==std::byte{2});release(pool,a,false);EXPECT(pool.pin_count(a)==0);}
}int main(){capture_replay();if(failures)return 1;std::cout<<"All mutation infrastructure tests passed\n";}
