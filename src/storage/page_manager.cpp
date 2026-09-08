#include "ddb/storage/page_manager.h"

#include <array>
#include <limits>
#include <string>

namespace ddb::storage {
namespace {
constexpr std::uintmax_t kPageSizeAsUintMax = static_cast<std::uintmax_t>(kPageSize);
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
  Page blank(id);
  file_.clear();
  file_.seekp(offset_for(id));
  ensure_stream_good("seek during allocation");
  file_.write(reinterpret_cast<const char*>(blank.data()), static_cast<std::streamsize>(kPageSize));
  ensure_stream_good("write during allocation");
  ++page_count_;
  return id;
}

void PageManager::read_page(PageId id, Page& page) {
  validate_allocated(id);
  file_.clear();
  file_.seekg(offset_for(id));
  ensure_stream_good("seek before read");
  file_.read(reinterpret_cast<char*>(page.data()), static_cast<std::streamsize>(kPageSize));
  if (file_.gcount() != static_cast<std::streamsize>(kPageSize)) {
    throw StorageError("short read from database file");
  }
  ensure_stream_good("read");
  page.set_id(id);
}

void PageManager::write_page(PageId id, const Page& page) {
  validate_allocated(id);
  if (page.id() != id) {
    throw StorageError("page identifier does not match write target");
  }
  file_.clear();
  file_.seekp(offset_for(id));
  ensure_stream_good("seek before write");
  file_.write(reinterpret_cast<const char*>(page.data()), static_cast<std::streamsize>(kPageSize));
  ensure_stream_good("write");
}

void PageManager::flush() {
  file_.flush();
  ensure_stream_good("flush");
}
}  // namespace ddb::storage
