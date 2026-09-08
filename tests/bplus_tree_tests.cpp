#include <filesystem>
#include <iostream>
#include <map>
#include <random>

#include "ddb/index/bplus_tree.h"

namespace {
int failures=0;
#define EXPECT(x) do { if(!(x)){++failures;std::cerr<<__FUNCTION__<<": " #x "\n";} } while(false)
using namespace ddb;
std::filesystem::path path(){auto p=std::filesystem::temp_directory_path()/"ddb_bplus_tree_test.db";std::error_code ec;std::filesystem::remove(p,ec);return p;}
index::RecordId value(index::KeyType key){return {storage::PageId(static_cast<std::uint64_t>(key+1000)),static_cast<std::uint32_t>(key)};}
void insertion_scan_deletion_persistence(){const auto db=path(); storage::PageId meta;{storage::PageManager disk(db);buffer::BufferPoolManager bp(disk,8);meta=index::BPlusTree::create(disk,bp,{3,3});index::BPlusTree tree(disk,bp,meta);index::RecordId out;EXPECT(!tree.get_value(1,out));EXPECT(tree.insert(1,value(1)));EXPECT(!tree.insert(1,value(9)));for(int k=2;k<=80;++k)EXPECT(tree.insert(k,value(k)));EXPECT(tree.height()>2);for(int k=1;k<=80;++k){EXPECT(tree.get_value(k,out));EXPECT(out==value(k));}auto all=tree.scan(10,20);EXPECT(all.size()==11);EXPECT(all.front().first==10&&all.back().first==20);std::cerr<<"CHECK after inserts\n";EXPECT(tree.validate_invariants());for(int k=1;k<=80;k+=2)EXPECT(tree.remove(k));std::cerr<<"CHECK after odd deletes\n";EXPECT(tree.validate_invariants());for(int k=2;k<=80;k+=2)EXPECT(tree.remove(k));std::cerr<<"CHECK after even deletes\n";EXPECT(tree.validate_invariants());EXPECT(!tree.remove(1));tree.flush();}{storage::PageManager disk(db);buffer::BufferPoolManager bp(disk,8);index::BPlusTree tree(disk,bp,meta);index::RecordId out;EXPECT(tree.scan(-100,100).empty());EXPECT(tree.height()==1);std::cerr<<"CHECK reopen\n";EXPECT(tree.validate_invariants());}std::error_code ec;std::filesystem::remove(db,ec);}
void randomized_reference_model(){const auto db=path();storage::PageManager disk(db);buffer::BufferPoolManager bp(disk,12);auto meta=index::BPlusTree::create(disk,bp,{4,4});index::BPlusTree tree(disk,bp,meta);std::map<int,index::RecordId> reference;std::mt19937 rng(42);for(int step=0;step<300;++step){int k=static_cast<int>(rng()%60);if(rng()%2==0){const bool inserted=tree.insert(k,value(k));const bool expected=reference.emplace(k,value(k)).second;EXPECT(inserted==expected);}else {const bool removed=tree.remove(k);const bool expected=reference.erase(k)!=0;EXPECT(removed==expected);}EXPECT(tree.validate_invariants());auto rows=tree.scan(-100,100);EXPECT(rows.size()==reference.size());std::size_t i=0;for(const auto& [key,rid]:reference){EXPECT(rows[i].first==key&&rows[i].second==rid);++i;}}}
}
int main(){try{insertion_scan_deletion_persistence();randomized_reference_model();}catch(const std::exception& e){++failures;std::cerr<<e.what()<<"\n";}if(failures){std::cerr<<failures<<" failures\n";return 1;}std::cout<<"All B+ tree tests passed\n";}
