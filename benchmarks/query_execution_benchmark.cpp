#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "ddb/execution/executor.h"

using namespace ddb;
using namespace ddb::execution;

namespace {
struct Measurement final { std::size_t tuples; double seconds; };

Measurement run(Executor& executor) {
  Tuple tuple;
  const auto start = std::chrono::steady_clock::now();
  executor.init();
  std::size_t tuples = 0;
  while (executor.next(tuple)) ++tuples;
  return {tuples, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()};
}

void print_result(const char* label, const Measurement& result) {
  std::cout << label << ": " << result.tuples << " tuples in " << result.seconds << " s, "
            << (result.tuples / result.seconds) << " tuples/s\n";
}
}

int main() {
  const auto file = std::filesystem::temp_directory_path() / "ddb_query_benchmark.db";
  std::error_code error;
  std::filesystem::remove(file, error);

  storage::PageManager page_manager(file);
  buffer::BufferPoolManager buffer_pool(page_manager, 64);
  const Schema schema({{"id", ValueType::Int64}, {"text", ValueType::String}});
  const auto metadata = TableHeap::create(page_manager, buffer_pool, schema);
  TableHeap heap(page_manager, buffer_pool, metadata, schema);
  const auto index_metadata = index::BPlusTree::create(page_manager, buffer_pool);
  index::BPlusTree index(page_manager, buffer_pool, index_metadata);

  constexpr int kRows = 10'000;
  for (int id = 0; id < kRows; ++id) {
    const auto record_id = heap.insert({{std::int64_t(id), "payload"}});
    if (!record_id || !index.insert(id, *record_id)) {
      throw std::runtime_error("benchmark setup failed to insert a unique indexed tuple");
    }
  }

  std::cout << std::fixed << std::setprecision(6);
  std::cout << "Dataset: " << kRows << " tuples, 19-byte serialized tuple; buffer pool: 64 pages\n"
            << "Build/run: Release, steady_clock, one warm-ish in-process run per operation\n";

  SeqScanPlan scan(heap);
  auto seq_scan = make_executor(scan);
  print_result("Sequential scan", run(*seq_scan));

  FilterPlan filter(std::make_shared<SeqScanPlan>(heap),
                    {0, Predicate::Op::Ge, std::int64_t(kRows / 2)});
  auto filtered = make_executor(filter);
  print_result("Filter (id >= 5000)", run(*filtered));

  FilterPlan equality_filter(std::make_shared<SeqScanPlan>(heap),
                             {0, Predicate::Op::Eq, std::int64_t(7777)});
  auto sequential_lookup = make_executor(equality_filter);
  print_result("Seq scan + equality filter", run(*sequential_lookup));

  IndexScanPlan lookup(heap, index, 7777);
  auto index_lookup = make_executor(lookup);
  print_result("Index equality lookup", run(*index_lookup));

  SortPlan sort(std::make_shared<SeqScanPlan>(heap), {{0, false}});
  auto sorted = make_executor(sort);
  print_result("In-memory sort (id DESC)", run(*sorted));

  constexpr int kJoinRows = 1'000;
  const auto join_metadata = TableHeap::create(page_manager, buffer_pool, schema);
  TableHeap join_heap(page_manager, buffer_pool, join_metadata, schema);
  for (int id = 0; id < kJoinRows; ++id) {
    if (!join_heap.insert({{std::int64_t(id), "join-payload"}})) {
      throw std::runtime_error("benchmark setup failed to insert join tuple");
    }
  }
  JoinPlan join(std::make_shared<SeqScanPlan>(join_heap),
                std::make_shared<SeqScanPlan>(join_heap), 0, 0);
  auto nested_loop_join = make_executor(join);
  print_result("Nested-loop equality join (1000 x 1000)", run(*nested_loop_join));
}
