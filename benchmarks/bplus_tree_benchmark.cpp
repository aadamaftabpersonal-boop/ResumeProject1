#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include "ddb/index/bplus_tree.h"

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kDatasetSize = 10'000;
constexpr std::size_t kBufferFrames = 256;
constexpr std::uint64_t kSeed = 123456789;
volatile std::uint64_t checksum = 0;

double elapsed_seconds(const Clock::time_point& start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
void report_ops(const char* name, std::size_t operations, double seconds) {
  const double safe=seconds>0?seconds:1e-9;
  std::cout<<name<<": operations="<<operations<<", seconds="<<std::fixed<<std::setprecision(6)<<seconds
           <<", ops_per_second="<<(operations/safe)<<", average_us="<<(safe*1e6/operations)<<"\n";
}
}
int main() {
  const auto path=std::filesystem::temp_directory_path()/"ddb_bplus_tree_benchmark.db";
  std::error_code ec; std::filesystem::remove(path,ec);
  try {
    std::mt19937_64 rng(kSeed);
    std::vector<std::int64_t> keys(kDatasetSize); std::iota(keys.begin(),keys.end(),0); std::shuffle(keys.begin(),keys.end(),rng);
    ddb::storage::PageManager disk(path); ddb::buffer::BufferPoolManager pool(disk,kBufferFrames);
    const auto meta=ddb::index::BPlusTree::create(disk,pool); ddb::index::BPlusTree tree(disk,pool,meta);
    auto start=Clock::now();
    for(const auto key:keys) { if(!tree.insert(key,{ddb::storage::PageId(static_cast<std::uint64_t>(key)),static_cast<std::uint32_t>(key)})) throw std::runtime_error("duplicate insertion"); }
    const double insert_seconds=elapsed_seconds(start); tree.flush();
    std::cout<<"=== B+ Tree Benchmark ===\nConfiguration: dataset="<<kDatasetSize<<", buffer_frames="<<kBufferFrames<<", page_size="<<ddb::storage::kPageSize<<", seed="<<kSeed<<"\n";
    std::cout<<"Tree: height="<<tree.height()<<", allocated_pages="<<disk.page_count()<<"\n"; report_ops("Insertion",kDatasetSize,insert_seconds);

    std::vector<std::int64_t> lookups; lookups.reserve(kDatasetSize); for(std::size_t i=0;i<kDatasetSize*8/10;++i)lookups.push_back(keys[i]); for(std::size_t i=0;i<kDatasetSize*2/10;++i)lookups.push_back(static_cast<std::int64_t>(kDatasetSize+i)); std::shuffle(lookups.begin(),lookups.end(),rng);
    std::size_t hits=0; ddb::index::RecordId record; start=Clock::now();
    for(auto key:lookups){if(tree.get_value(key,record)){++hits;checksum+=record.slot_id;}}
    const double tree_lookup_seconds=elapsed_seconds(start); report_ops("B+ tree point lookup",lookups.size(),tree_lookup_seconds); std::cout<<"Lookup results: hits="<<hits<<", misses="<<(lookups.size()-hits)<<"\n";

    std::vector<std::int64_t> linear(kDatasetSize); std::iota(linear.begin(),linear.end(),0); std::size_t linear_hits=0; start=Clock::now();
    for(auto key:lookups){const auto found=std::find(linear.begin(),linear.end(),key);if(found!=linear.end()){++linear_hits;checksum+=static_cast<std::uint64_t>(*found);}}
    const double linear_seconds=elapsed_seconds(start); report_ops("Linear baseline lookup",lookups.size(),linear_seconds); std::cout<<"Baseline hits="<<linear_hits<<"\n";

    constexpr std::size_t kScans=100; constexpr std::size_t kRangeWidth=100; std::size_t rows=0; start=Clock::now();
    for(std::size_t i=0;i<kScans;++i){const auto low=static_cast<std::int64_t>((i*97)%(kDatasetSize-kRangeWidth));auto result=tree.scan(low,low+static_cast<std::int64_t>(kRangeWidth-1));rows+=result.size();for(const auto& [key,rid]:result)checksum+=static_cast<std::uint64_t>(key)+rid.slot_id;}
    const double scan_seconds=elapsed_seconds(start); const double safe=scan_seconds>0?scan_seconds:1e-9;
    std::cout<<"Range scan: scans="<<kScans<<", range_width="<<kRangeWidth<<", records="<<rows<<", seconds="<<std::fixed<<std::setprecision(6)<<scan_seconds<<", scans_per_second="<<(kScans/safe)<<", records_per_second="<<(rows/safe)<<", average_scan_us="<<(safe*1e6/kScans)<<"\n";
    std::cout<<"Checksum="<<checksum<<"\n";
  } catch(const std::exception& error) {std::cerr<<"benchmark failed: "<<error.what()<<"\n";return 1;}
  std::filesystem::remove(path,ec);
}
