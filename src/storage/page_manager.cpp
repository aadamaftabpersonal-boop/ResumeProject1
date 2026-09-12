#include "ddb/storage/page_manager.h"

#include <array>
#include <cstring>
#include <limits>
#include <string>

namespace ddb::storage {
namespace {
constexpr std::uintmax_t kPageSizeAsUintMax = static_cast<std::uintmax_t>(kPageSize);
constexpr char kMagic[]="DDBP";
constexpr char kCatalogMagic[]="DDBA";
constexpr std::uint16_t kCatalogVersion = 1;
constexpr std::size_t kCatalogHeader = 24;
constexpr std::size_t kCatalogBits = (kPageSize-kCatalogHeader)*8;
void put64(std::byte* d,std::size_t p,std::uint64_t v){for(unsigned i=0;i<8;++i)d[p+i]=std::byte((v>>(i*8))&255U);}std::uint64_t get64(const std::byte*d,std::size_t p){std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint64_t(std::to_integer<unsigned char>(d[p+i]))<<(i*8);return v;}void put16(std::byte*d,std::size_t p,std::uint16_t v){d[p]=std::byte(v&255U);d[p+1]=std::byte(v>>8U);}std::uint16_t get16(const std::byte*d,std::size_t p){return std::uint16_t(std::to_integer<unsigned char>(d[p]))|(std::uint16_t(std::to_integer<unsigned char>(d[p+1]))<<8U);}
}

PageManager::PageManager(const std::filesystem::path& database_path) : path_(database_path) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(path_, ec);
  if (ec) {
    throw StorageError("cannot inspect database file: " + ec.message());
  }
  if (!exists) {
    std::ofstream creator(path_, std::ios::binary);
    if (!creator) {
      throw StorageError("cannot create database file: " + path_.string());
    }
    creator.close();
  }

  const std::uintmax_t length = std::filesystem::file_size(path_, ec);
  if (ec) {
    throw StorageError("cannot determine database file size: " + ec.message());
  }
  if (length % kPageSizeAsUintMax != 0U) {
    throw StorageError("database file is truncated or corrupt (not page-aligned)");
  }
  file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file_.is_open()) {
    throw StorageError("cannot open database file for read/write: " + path_.string());
  }
  allocation_bitmap_.assign(kPageSize-kCatalogHeader,std::byte{0});
  if (length == 0) { write_allocation_catalog(); file_.flush(); return; }
  std::array<std::byte,kPageSize> catalog{}; file_.seekg(0); file_.read(reinterpret_cast<char*>(catalog.data()), static_cast<std::streamsize>(catalog.size()));
  if (file_.gcount()!=static_cast<std::streamsize>(catalog.size()) || std::memcmp(catalog.data(),kCatalogMagic,4)!=0 || get16(catalog.data(),4)!=kCatalogVersion) throw StorageError("legacy or corrupt allocation catalog");
  const auto stored_pages=get64(catalog.data(),8); const auto stored_allocated=get64(catalog.data(),16);
  page_count_=static_cast<std::uint64_t>(length/kPageSizeAsUintMax)-1;
  if(stored_pages!=page_count_ || page_count_>kCatalogBits) throw StorageError("invalid allocation catalog bounds");
  std::memcpy(allocation_bitmap_.data(),catalog.data()+kCatalogHeader,allocation_bitmap_.size());
  for(std::uint64_t i=0;i<page_count_;++i) if(is_page_allocated(PageId(i))) ++allocated_page_count_;
  if(allocated_page_count_!=stored_allocated) throw StorageError("invalid allocation catalog count");
}

PageManager::~PageManager() noexcept {
  if (file_.is_open()) {
    file_.flush();
  }
}

std::streamoff PageManager::offset_for(PageId id) const {
  if (!id.is_valid()) {
    throw StorageError("invalid page id");
  }
  constexpr auto kMaxOffset = static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
  if (id.value() > kMaxOffset / kPageSize) {
    throw StorageError("page id cannot be represented as a file offset");
  }
  return static_cast<std::streamoff>((id.value() + 1U) * kPageSize);
}

void PageManager::validate_allocated(PageId id) const {
  (void)offset_for(id);
  if (id.value() >= page_count_) {
    throw StorageError("page id is beyond the allocated database file");
  }
}

bool PageManager::is_page_allocated(PageId id) const noexcept { return id.is_valid() && id.value()<page_count_ && (std::to_integer<unsigned char>(allocation_bitmap_[id.value()/8]) & (1U<<(id.value()%8)))!=0; }
void PageManager::validate_logically_allocated(PageId id) const { validate_allocated(id); if(!is_page_allocated(id)) throw StorageError("page is physically present but logically unallocated"); }
void PageManager::write_allocation_catalog() { std::array<std::byte,kPageSize> raw{}; std::memcpy(raw.data(),kCatalogMagic,4); put16(raw.data(),4,kCatalogVersion); put64(raw.data(),8,page_count_); put64(raw.data(),16,allocated_page_count_); std::memcpy(raw.data()+kCatalogHeader,allocation_bitmap_.data(),allocation_bitmap_.size()); file_.clear(); file_.seekp(0); file_.write(reinterpret_cast<const char*>(raw.data()),static_cast<std::streamsize>(raw.size())); ensure_stream_good("write allocation catalog"); }

void PageManager::ensure_stream_good(const char* operation) {
  if (!file_) {
    throw StorageError(std::string(operation) + " failed for database file: " + path_.string());
  }
}

PageId PageManager::allocate_page() {
  if(page_count_>=kCatalogBits) throw StorageError("allocation catalog capacity exhausted");
  const PageId id(page_count_);
  (void)offset_for(id);
  ++page_count_;
  try { file_.clear(); file_.seekp(offset_for(id)); Page page(id); std::array<std::byte,kPageSize> raw{};std::memcpy(raw.data(),kMagic,4);put16(raw.data(),4,kPageFormatVersion);put16(raw.data(),6,kPageHeaderSize);put64(raw.data(),8,id.value());put64(raw.data(),16,0);file_.write(reinterpret_cast<const char*>(raw.data()),static_cast<std::streamsize>(raw.size()));ensure_stream_good("write new page"); allocation_bitmap_[id.value()/8]|=std::byte(1U<<(id.value()%8));++allocated_page_count_;write_allocation_catalog(); } catch(...) { --page_count_; throw; }
  return id;
}

void PageManager::deallocate_page(PageId id) { validate_logically_allocated(id); allocation_bitmap_[id.value()/8]&=std::byte(~(1U<<(id.value()%8)));--allocated_page_count_;write_allocation_catalog(); }

void PageManager::read_page(PageId id, Page& page) {
  validate_logically_allocated(id);
  file_.clear();
  file_.seekg(offset_for(id));
  ensure_stream_good("seek before read");
  std::array<std::byte,kPageSize> raw{};file_.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(kPageSize));
  if (file_.gcount() != static_cast<std::streamsize>(kPageSize)) {
    throw StorageError("short read from database file");
  }
  ensure_stream_good("read");
  if(std::memcmp(raw.data(),kMagic,4)!=0||get16(raw.data(),4)!=kPageFormatVersion||get16(raw.data(),6)!=kPageHeaderSize)throw StorageError("unsupported or legacy page format");
  if(get64(raw.data(),8)!=id.value())throw StorageError("page header identifier mismatch");
  page.set_id(id);page.set_lsn(get64(raw.data(),16));std::memcpy(page.data(),raw.data()+kPageHeaderSize,kPagePayloadSize);
}

void PageManager::write_page(PageId id, const Page& page) {
  validate_logically_allocated(id);
  if (page.id() != id) {
    throw StorageError("page identifier does not match write target");
  }
  file_.clear();
  file_.seekp(offset_for(id));
  ensure_stream_good("seek before write");
  std::array<std::byte,kPageSize> raw{};std::memcpy(raw.data(),kMagic,4);put16(raw.data(),4,kPageFormatVersion);put16(raw.data(),6,kPageHeaderSize);put64(raw.data(),8,id.value());put64(raw.data(),16,page.lsn());std::memcpy(raw.data()+kPageHeaderSize,page.data(),kPagePayloadSize);file_.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(kPageSize));
  ensure_stream_good("write");
}

void PageManager::flush() {
  file_.flush();
  ensure_stream_good("flush");
}
}  // namespace ddb::storage
