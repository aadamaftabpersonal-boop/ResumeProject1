#include <filesystem>
#include <iostream>
#include "ddb/index/bplus_tree.h"
namespace { int failures=0;
#define EXPECT(x) do{if(!(x)){++failures;std::cerr<<#x<<"\n";}}while(false)
using namespace ddb;
std::filesystem::path path(){auto p=std::filesystem::temp_directory_path()/"ddb_bplus_mutations.db";std::error_code e;std::filesystem::remove(p,e);return p;}
index::RecordId value(int key){return {storage::PageId(static_cast<std::uint64_t>(key+100)),static_cast<std::uint32_t>(key)};}
void capture_paths(){auto file=path();storage::PageManager disk(file);buffer::BufferPoolManager pool(disk,32);auto meta=index::BPlusTree::create(disk,pool,{3,3});index::BPlusTree tree(disk,pool,meta);storage::MutationContext first(pool);EXPECT(tree.insert(1,value(1),&first));auto simple=first.finalize();EXPECT(simple.size()==1&&simple[0].before_image!=simple[0].after_image);storage::MutationContext split(pool);EXPECT(tree.insert(2,value(2),&split));EXPECT(tree.insert(3,value(3),&split));EXPECT(tree.insert(4,value(4),&split));auto mutations=split.finalize();EXPECT(mutations.size()>=3);for(std::size_t i=0;i<mutations.size();++i)EXPECT(mutations[i].sequence==i);for(auto it=mutations.rbegin();it!=mutations.rend();++it)storage::apply_before_image(pool,*it);for(const auto& m:mutations)storage::apply_after_image(pool,m);EXPECT(tree.validate_invariants());storage::MutationContext noop(pool);EXPECT(!tree.insert(1,value(1),&noop));EXPECT(noop.finalize().empty());EXPECT(pool.validate_invariants());}
}
int main(){capture_paths();if(failures)return 1;std::cout<<"All B+ mutation tests passed\n";}
