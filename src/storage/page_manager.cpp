#include "ddb/storage/page_manager.h"

#include <array>
#include <cstring>
#include <limits>
#include <string>

namespace ddb::storage {
namespace {
constexpr std::uintmax_t kPageSizeAsUintMax = static_cast<std::uintmax_t>(kPageSize);
constexpr char kMagic[]="DDBP";
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
  page_count_ = static_cast<std::uint64_t>(length / kPageSizeAsUintMax);

  file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file_.is_open()) {
    throw StorageError("cannot open database file for read/write: " + path_.string());
  }
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
  return static_cast<std::streamoff>(id.value() * kPageSize);
}

void PageManager::validate_allocated(PageId id) const {
  (void)offset_for(id);
  if (id.value() >= page_count_) {
    throw StorageError("page id is beyond the allocated database file");
  }
}

void PageManager::ensure_stream_good(const char* operation) {
  if (!file_) {
    throw StorageError(std::string(operation) + " failed for database file: " + path_.string());
  }
}

PageId PageManager::allocate_page() {
  const PageId id(page_count_);
  (void)offset_for(id);
  ++page_count_;
  try { write_page(id,Page(id)); } catch(...) { --page_count_; throw; }
  return id;
}

void PageManager::read_page(PageId id, Page& page) {
  validate_allocated(id);
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
  validate_allocated(id);
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
