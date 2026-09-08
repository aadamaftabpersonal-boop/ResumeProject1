#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ddb/buffer/buffer_pool_manager.h"

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kWorkingSetPages = 8;
constexpr std::size_t kOperations = 10'000;

template <typename Operation>
double measure(const char* name, Operation operation) {
  const auto start = Clock::now();
  operation();
  const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
  const double safe_elapsed = elapsed > 0.0 ? elapsed : 1.0e-9;
  std::cout << std::fixed << std::setprecision(3) << name << ": operations=" << kOperations
            << ", total_seconds=" << elapsed << ", ops_per_second=" << (kOperations / safe_elapsed)
            << ", average_latency_us=" << (safe_elapsed * 1'000'000.0 / kOperations) << "\n";
  return elapsed;
}
}

int main() {
  const auto path = std::filesystem::temp_directory_path() / "ddb_buffer_pool_benchmark.db";
  std::error_code ec; std::filesystem::remove(path, ec);
  try {
    ddb::storage::PageManager disk(path);
    std::vector<ddb::storage::PageId> pages;
    pages.reserve(kWorkingSetPages);
    for (std::size_t index = 0; index < kWorkingSetPages; ++index) pages.push_back(disk.allocate_page());

    ddb::storage::Page direct_page;
    measure("direct PageManager repeated reads", [&] {
      for (std::size_t operation = 0; operation < kOperations; ++operation) {
        disk.read_page(pages[operation % pages.size()], direct_page);
      }
    });
    std::cout << "direct PageManager: disk_reads=" << kOperations << ", disk_writes=0\n";

    ddb::buffer::BufferPoolManager pool(disk, kWorkingSetPages);
    measure("BufferPoolManager repeated reads", [&] {
      for (std::size_t operation = 0; operation < kOperations; ++operation) {
        const auto id = pages[operation % pages.size()];
        auto* page = pool.fetch_page(id);
        if (page == nullptr || !pool.unpin_page(id, false)) {
          throw std::runtime_error("benchmark buffer-pool operation failed");
        }
      }
    });
    const auto& stats = pool.stats();
    const double hit_rate = stats.fetches == 0 ? 0.0 : (100.0 * static_cast<double>(stats.cache_hits) / static_cast<double>(stats.fetches));
    std::cout << std::fixed << std::setprecision(2)
              << "BufferPoolManager: cache_hit_rate_percent=" << hit_rate
              << ", disk_reads=" << stats.disk_reads << ", disk_writes=" << stats.disk_writes << "\n";
  } catch (const std::exception& error) { std::cerr << "benchmark failed: " << error.what() << "\n"; return 1; }
  std::filesystem::remove(path, ec);
}
