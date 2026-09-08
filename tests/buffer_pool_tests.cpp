#include <array>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "ddb/buffer/buffer_pool_manager.h"

namespace {
using ddb::buffer::BufferPoolManager;
using ddb::storage::PageId;
using ddb::storage::PageManager;

int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { ++failures; std::cerr << __FUNCTION__ << ": expectation failed: " #condition "\n"; } } while (false)

std::filesystem::path test_path(std::string_view name) {
  const auto path = std::filesystem::temp_directory_path() / ("ddb_buffer_" + std::string(name) + ".db");
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return path;
}

std::vector<PageId> allocate(PageManager& manager, std::size_t count) {
  std::vector<PageId> ids;
  ids.reserve(count);
  for (std::size_t index = 0; index < count; ++index) ids.push_back(manager.allocate_page());
  return ids;
}

void basic_pinning_and_hits() {
  const auto path = test_path("basic");
  PageManager disk(path);
  const auto ids = allocate(disk, 2);
  BufferPoolManager pool(disk, 2);
  EXPECT(pool.capacity() == 2);
  EXPECT(pool.cached_page_count() == 0);
  BufferPoolManager empty_pool(disk, 0);
  EXPECT(empty_pool.fetch_page(ids[0]) == nullptr);
  EXPECT(empty_pool.validate_invariants());
  EXPECT(pool.fetch_page(PageId()) == nullptr);
  EXPECT(!pool.unpin_page(PageId(), false));
  EXPECT(!pool.flush_page(PageId()));
  auto* first = pool.fetch_page(ids[0]);
  EXPECT(first != nullptr);
  EXPECT(pool.contains(ids[0]));
  EXPECT(pool.pin_count(ids[0]) == 1U);
  EXPECT(pool.fetch_page(ids[0]) == first);
  EXPECT(pool.pin_count(ids[0]) == 2U);
  EXPECT(pool.stats().cache_hits == 1);
  EXPECT(pool.unpin_page(ids[0], false));
  EXPECT(pool.pin_count(ids[0]) == 1U);
  EXPECT(pool.unpin_page(ids[0], false));
  EXPECT(pool.pin_count(ids[0]) == 0U);
  EXPECT(!pool.unpin_page(ids[0], false));
  EXPECT(pool.validate_invariants());
  std::error_code ec; std::filesystem::remove(path, ec);
}

void lru_and_pinned_pool() {
  const auto path = test_path("lru");
  PageManager disk(path);
  const auto ids = allocate(disk, 5);
  BufferPoolManager pool(disk, 3);
  for (std::size_t index = 0; index < 3; ++index) EXPECT(pool.fetch_page(ids[index]) != nullptr);
  for (std::size_t index = 0; index < 3; ++index) EXPECT(pool.unpin_page(ids[index], false));
  EXPECT(pool.fetch_page(ids[0]) != nullptr); EXPECT(pool.unpin_page(ids[0], false));
  EXPECT(pool.fetch_page(ids[1]) != nullptr); EXPECT(pool.unpin_page(ids[1], false));
  EXPECT(pool.fetch_page(ids[3]) != nullptr);
  EXPECT(!pool.contains(ids[2]));  // C is least recently used after A and B are touched.
  EXPECT(pool.stats().disk_writes == 0);  // C was clean, so its eviction requires no write.
  EXPECT(pool.validate_invariants());
  BufferPoolManager pinned_pool(disk, 1);
  EXPECT(pinned_pool.fetch_page(ids[4]) != nullptr);
  EXPECT(pinned_pool.fetch_page(ids[2]) == nullptr);
  EXPECT(pinned_pool.validate_invariants());
  std::error_code ec; std::filesystem::remove(path, ec);
}

void dirty_eviction_and_flush() {
  const auto path = test_path("dirty");
  {
    PageManager disk(path);
    const auto ids = allocate(disk, 2);
    BufferPoolManager pool(disk, 1);
    auto* page = pool.fetch_page(ids[0]);
    EXPECT(page != nullptr);
    page->data()[0] = std::byte{0xAB};
    EXPECT(pool.unpin_page(ids[0], true));
    EXPECT(pool.is_dirty(ids[0]) == true);
    EXPECT(pool.fetch_page(ids[1]) != nullptr);  // Evicts and writes page 0.
    EXPECT(!pool.contains(ids[0]));
    EXPECT(pool.stats().disk_writes == 1);
    EXPECT(pool.unpin_page(ids[1], false));

    auto* reloaded = pool.fetch_page(ids[0]);
    EXPECT(reloaded != nullptr);
    EXPECT(reloaded->data()[0] == std::byte{0xAB});
    reloaded->data()[1] = std::byte{0xCD};
    EXPECT(pool.unpin_page(ids[0], true));
    EXPECT(pool.flush_page(ids[0]));
    EXPECT(pool.is_dirty(ids[0]) == false);
    EXPECT(pool.validate_invariants());
  }
  {
    PageManager disk(path);
    ddb::storage::Page page;
    disk.read_page(PageId(0), page);
    EXPECT(page.data()[0] == std::byte{0xAB});
    EXPECT(page.data()[1] == std::byte{0xCD});
  }
  std::error_code ec; std::filesystem::remove(path, ec);
}

void deletion_and_stress() {
  const auto path = test_path("stress");
  PageManager disk(path);
  const auto ids = allocate(disk, 5);
  {
    BufferPoolManager pool(disk, 2);
    EXPECT(pool.fetch_page(ids[0]) != nullptr);
    EXPECT(!pool.delete_page(ids[0]));
    EXPECT(pool.unpin_page(ids[0], false));
    EXPECT(pool.delete_page(ids[0]));
    EXPECT(!pool.contains(ids[0]));
    EXPECT(pool.delete_page(ids[4]));
    for (std::size_t round = 1; round <= 20; ++round) {
      for (const PageId id : ids) {
        auto* page = pool.fetch_page(id);
        EXPECT(page != nullptr);
        page->data()[0] = std::byte{static_cast<unsigned char>(round)};
        EXPECT(pool.unpin_page(id, true));
        EXPECT(pool.validate_invariants());
      }
    }
    pool.flush_all_pages();
    EXPECT(pool.validate_invariants());
  }
  for (const PageId id : ids) {
    ddb::storage::Page page;
    disk.read_page(id, page);
    EXPECT(page.data()[0] == std::byte{20});
  }
  std::error_code ec; std::filesystem::remove(path, ec);
}

void run(std::string_view name, const std::function<void()>& test) {
  const int before = failures;
  try { test(); } catch (const std::exception& error) { ++failures; std::cerr << name << ": unexpected exception: " << error.what() << "\n"; }
  if (failures == before) std::cout << "PASS " << name << "\n";
}
}  // namespace

int main() {
  run("basic_pinning_and_hits", basic_pinning_and_hits);
  run("lru_and_pinned_pool", lru_and_pinned_pool);
  run("dirty_eviction_and_flush", dirty_eviction_and_flush);
  run("deletion_and_stress", deletion_and_stress);
  if (failures != 0) { std::cerr << failures << " test expectation(s) failed\n"; return 1; }
  std::cout << "All buffer-pool tests passed\n";
}
