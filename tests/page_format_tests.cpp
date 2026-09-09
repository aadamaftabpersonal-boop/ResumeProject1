#include <filesystem>
#include <fstream>
#include <iostream>
#include "ddb/storage/page_manager.h"
namespace {
int failures=0;
#define EXPECT(x) do{if(!(x)){++failures;std::cerr<<#x<<"\n";}}while(false)
using namespace ddb::storage;std::filesystem::path path(const char*n){auto p=std::filesystem::temp_directory_path()/n;std::error_code e;std::filesystem::remove(p,e);return p;}
void format_and_lsn(){auto p=path("ddb_page_format.db");{PageManager m(p);auto id=m.allocate_page();Page x(id);EXPECT(x.version()==1);EXPECT(x.payload_size()==4064);EXPECT(x.size()==4064);x.set_lsn(99);x.data()[0]=std::byte{0x42};m.write_page(id,x);m.flush();}{PageManager m(p);Page x;m.read_page(PageId(0),x);EXPECT(x.lsn()==99);EXPECT(x.data()[0]==std::byte{0x42});}std::error_code e;std::filesystem::remove(p,e);}
void invalid_headers(){auto p=path("ddb_page_bad.db");{std::ofstream f(p,std::ios::binary);char zero[4096]{};f.write(zero,sizeof zero);}try{PageManager m(p);Page x;m.read_page(PageId(0),x);EXPECT(false);}catch(const StorageError&){EXPECT(true);}std::error_code e;std::filesystem::remove(p,e);}
void corrupt_header_field(std::size_t offset,std::byte value){auto p=path("ddb_page_corrupt.db");{PageManager m(p);const auto id=m.allocate_page();EXPECT(id==PageId(0));m.flush();}{std::fstream f(p,std::ios::binary|std::ios::in|std::ios::out);f.seekp(static_cast<std::streamoff>(offset));const char c=static_cast<char>(std::to_integer<unsigned char>(value));f.write(&c,1);}try{PageManager m(p);Page x;m.read_page(PageId(0),x);EXPECT(false);}catch(const StorageError&){EXPECT(true);}std::error_code e;std::filesystem::remove(p,e);}
void corruption_tests(){corrupt_header_field(0,std::byte{0});corrupt_header_field(4,std::byte{2});corrupt_header_field(6,std::byte{0});}
}int main(){format_and_lsn();invalid_headers();corruption_tests();if(failures)return 1;std::cout<<"All page format tests passed\n";}
