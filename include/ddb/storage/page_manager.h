#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "ddb/storage/page.h"

namespace ddb::recovery { class ReplayContext; }
namespace ddb::storage {
class LogManager;

class StorageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Owns one database file and provides synchronous, fixed-page I/O.
class PageManager final {
 public:
  explicit PageManager(const std::filesystem::path& database_path);
  ~PageManager() noexcept;

  PageManager(const PageManager&) = delete;
  PageManager& operator=(const PageManager&) = delete;
  PageManager(PageManager&&) = delete;
  PageManager& operator=(PageManager&&) = delete;

  [[nodiscard]] PageId allocate_page();
  [[nodiscard]] PageId reserve_page();
  void activate_reserved_page(PageId id);
  [[nodiscard]] PageId allocate_transactional_page(LogManager&, std::uint64_t transaction_id);
  // Logical deallocation retains the physical slot; it is deliberately not reused.
  void deallocate_page(PageId id);
  [[nodiscard]] bool is_page_allocated(PageId id) const noexcept;
  [[nodiscard]] std::uint64_t allocated_page_count() const noexcept { return allocated_page_count_; }
  void read_page(PageId id, Page& page);
  void write_page(PageId id, const Page& page);
  void flush();

  [[nodiscard]] std::uint64_t page_count() const noexcept { return page_count_; }

 private:
  friend class ddb::recovery::ReplayContext;
  // Creates exactly the next physical page and deliberately leaves its logical
  // allocation bit clear. Future allocation REDO decides whether to activate.
  void materialize_page_for_recovery(PageId id);
  [[nodiscard]] std::streamoff offset_for(PageId id) const;
  void validate_allocated(PageId id) const;
  void ensure_stream_good(const char* operation);

  std::filesystem::path path_;
  std::fstream file_;
  std::uint64_t page_count_{0};
  std::vector<std::byte> allocation_bitmap_;
  std::uint64_t allocated_page_count_{0};
  void write_allocation_catalog();
  void validate_logically_allocated(PageId id) const;
};
}  // namespace ddb::storage
