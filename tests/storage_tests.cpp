#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ddb/storage/page_manager.h"

namespace {
using ddb::storage::Page;
using ddb::storage::PageId;
using ddb::storage::PageManager;
using ddb::storage::StorageError;

int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { ++failures; std::cerr << __FUNCTION__ << ": expectation failed: " #condition "\n"; } } while (false)
#define EXPECT_THROW(expression) do { bool did_throw = false; try { (expression); } catch (const StorageError&) { did_throw = true; } EXPECT(did_throw); } while (false)

std::filesystem::path test_path(std::string_view name) {
  const auto path = std::filesystem::temp_directory_path() / ("ddb_storage_" + std::string(name) + ".db");
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return path;
}

void page_basics() {
  Page page(PageId(7));
  EXPECT(page.size() == ddb::storage::kPageSize);
  EXPECT(page.id() == PageId(7));
  for (std::size_t i = 0; i < page.size(); ++i) EXPECT(page.data()[i] == std::byte{0});
  page.data()[3] = std::byte{0xA5};
  EXPECT(page.data()[3] == std::byte{0xA5});
  page.clear();
  EXPECT(page.data()[3] == std::byte{0});
}

void allocation_and_io() {
  const auto path = test_path("allocation");
  {
    PageManager manager(path);
    EXPECT(manager.page_count() == 0);
    const auto zero = manager.allocate_page();
    const auto one = manager.allocate_page();
    const auto two = manager.allocate_page();
    EXPECT(zero == PageId(0)); EXPECT(one == PageId(1)); EXPECT(two == PageId(2));
    Page source(one);
    for (std::size_t i = 0; i < source.size(); ++i) source.data()[i] = std::byte{static_cast<unsigned char>(i % 251U)};
    manager.write_page(one, source);
    manager.flush();
    Page loaded;
    manager.read_page(one, loaded);
    EXPECT(loaded.id() == one);
    EXPECT(std::memcmp(source.data(), loaded.data(), source.size()) == 0);
  }
  std::error_code ec; std::filesystem::remove(path, ec);
}

void persistence_and_continuity() {
  const auto path = test_path("persistence");
  {
    PageManager manager(path);
    const auto first = manager.allocate_page();
    const auto second = manager.allocate_page();
    Page page(second);
    constexpr char kMessage[] = "persistent page data";
    std::memcpy(page.data(), kMessage, sizeof(kMessage));
    manager.write_page(second, page);
    manager.flush();
    EXPECT(first == PageId(0));
  }
  {
    PageManager manager(path);
    EXPECT(manager.page_count() == 2);
    const auto third = manager.allocate_page();
    EXPECT(third == PageId(2));
    Page page;
    manager.read_page(PageId(1), page);
    EXPECT(std::memcmp(page.data(), "persistent page data", sizeof("persistent page data")) == 0);
  }
  std::error_code ec; std::filesystem::remove(path, ec);
}

void invalid_and_corrupt_inputs() {
  const auto impossible = std::filesystem::temp_directory_path() / "ddb_missing_parent" / "database.db";
  EXPECT_THROW(PageManager(impossible));
  const auto path = test_path("errors");
  {
    PageManager manager(path);
    const auto page_id = manager.allocate_page();
    Page page(page_id);
    EXPECT_THROW(manager.read_page(PageId(1), page));
    EXPECT_THROW(manager.read_page(PageId(), page));
    EXPECT_THROW(manager.write_page(PageId(1), page));
    EXPECT_THROW(manager.write_page(page_id, Page(PageId(99))));
  }
  std::error_code ec; std::filesystem::remove(path, ec);
  const auto corrupt = test_path("corrupt");
  { std::ofstream out(corrupt, std::ios::binary); out << "not a page"; }
  EXPECT_THROW(PageManager(corrupt));
  std::filesystem::remove(corrupt, ec);
}

int run(std::string_view name, const std::function<void()>& test) {
  const int before = failures;
  try { test(); } catch (const std::exception& error) { ++failures; std::cerr << name << ": unexpected exception: " << error.what() << "\n"; }
  if (failures == before) std::cout << "PASS " << name << "\n";
  return failures == before ? 0 : 1;
}
}  // namespace

int main() {
  run("page_basics", page_basics);
  run("allocation_and_io", allocation_and_io);
  run("persistence_and_continuity", persistence_and_continuity);
  run("invalid_and_corrupt_inputs", invalid_and_corrupt_inputs);
  if (failures != 0) { std::cerr << failures << " test expectation(s) failed\n"; return 1; }
  std::cout << "All storage tests passed\n";
}
