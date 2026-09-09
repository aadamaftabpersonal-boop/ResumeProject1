#include <filesystem>
#include <iostream>
#include <set>
#include "ddb/execution/table_heap.h"

namespace {
int failures = 0;
#define EXPECT(x) do { if (!(x)) { ++failures; std::cerr << __FUNCTION__ << ": " #x "\n"; } } while (false)
using namespace ddb;
using namespace execution;

std::filesystem::path path() {
  auto file = std::filesystem::temp_directory_path() / "ddb_tableheap_mutations.db";
  std::error_code error;
  std::filesystem::remove(file, error);
  return file;
}

Schema schema() { return Schema({{"value", ValueType::String}}); }
Tuple row(std::size_t size, char value = 'x') { return Tuple{{std::string(size, value)}}; }

std::vector<std::byte> payload(buffer::BufferPoolManager& pool, storage::PageId id) {
  auto* page = pool.fetch_page(id);
  if (!page) throw std::runtime_error("cannot fetch test page");
  std::vector<std::byte> copy(page->data(), page->data() + page->payload_size());
  if (!pool.unpin_page(id, false)) throw std::runtime_error("cannot unpin test page");
  return copy;
}

void verify_replay(buffer::BufferPoolManager& pool, const std::vector<storage::PhysicalMutation>& mutations) {
  for (auto mutation = mutations.rbegin(); mutation != mutations.rend(); ++mutation) {
    storage::apply_before_image(pool, *mutation);
    EXPECT(payload(pool, mutation->page_id) == mutation->before_image);
  }
  for (const auto& mutation : mutations) {
    storage::apply_after_image(pool, mutation);
    EXPECT(payload(pool, mutation.page_id) == mutation.after_image);
  }
}

void simple_insert_and_replay() {
  auto file = path();
  storage::PageManager disk(file);
  buffer::BufferPoolManager pool(disk, 8);
  const auto metadata = TableHeap::create(disk, pool, schema());
  TableHeap table(disk, pool, metadata, schema());

  storage::MutationContext first(pool);
  const auto first_id = table.insert(row(32, 'a'), &first);
  EXPECT(first_id.has_value());
  auto first_mutations = first.finalize();
  EXPECT(first_mutations.size() == 3);
  EXPECT(first_mutations[0].page_id == first_id->page_id);
  EXPECT(first_mutations[1].page_id == metadata);
  EXPECT(first_mutations[2].page_id == first_id->page_id);
  for (std::size_t i = 0; i < first_mutations.size(); ++i) EXPECT(first_mutations[i].sequence == i);
  verify_replay(pool, first_mutations);

  storage::MutationContext second(pool);
  const auto second_id = table.insert(row(32, 'b'), &second);
  EXPECT(second_id.has_value() && second_id->page_id == first_id->page_id);
  auto mutations = second.finalize();
  EXPECT(mutations.size() == 1 && mutations[0].page_id == second_id->page_id);
  const auto post = payload(pool, second_id->page_id);
  storage::apply_before_image(pool, mutations[0]);
  EXPECT(payload(pool, second_id->page_id) == mutations[0].before_image);
  EXPECT(!table.get(*second_id).has_value());
  storage::apply_after_image(pool, mutations[0]);
  EXPECT(payload(pool, second_id->page_id) == post);
  EXPECT(table.get(*second_id).has_value());
  EXPECT(pool.pin_count(second_id->page_id).value_or(1) == 0);
  EXPECT(pool.validate_invariants());
}

void new_page_link_metadata_and_delete() {
  auto file = path();
  storage::PageManager disk(file);
  buffer::BufferPoolManager pool(disk, 8);
  const auto metadata = TableHeap::create(disk, pool, schema());
  TableHeap table(disk, pool, metadata, schema());
  const auto first = table.insert(row(3000, 'a'));
  EXPECT(first.has_value());

  storage::MutationContext grow(pool);
  const auto second = table.insert(row(3000, 'b'), &grow);
  EXPECT(second.has_value() && second->page_id != first->page_id);
  auto mutations = grow.finalize();
  EXPECT(mutations.size() == 4);
  EXPECT(mutations[0].page_id == second->page_id); // initialize new heap page
  EXPECT(mutations[1].page_id == first->page_id);  // previous-page next link
  EXPECT(mutations[2].page_id == metadata);        // first/last metadata
  EXPECT(mutations[3].page_id == second->page_id); // tuple + slot directory + free space
  for (std::size_t i = 0; i < mutations.size(); ++i) EXPECT(mutations[i].sequence == i);
  verify_replay(pool, mutations);

  storage::MutationContext erase(pool);
  EXPECT(table.erase(*second, &erase));
  auto deleted = erase.finalize();
  EXPECT(deleted.size() == 1 && deleted[0].page_id == second->page_id);
  EXPECT(!table.get(*second).has_value());
  storage::apply_before_image(pool, deleted[0]);
  EXPECT(table.get(*second).has_value());
  storage::apply_after_image(pool, deleted[0]);
  EXPECT(!table.get(*second).has_value());

  storage::MutationContext noop(pool);
  EXPECT(!table.erase(*second, &noop));
  EXPECT(noop.finalize().empty());
  EXPECT(pool.pin_count(first->page_id).value_or(1) == 0);
  EXPECT(pool.pin_count(second->page_id).value_or(1) == 0);
  EXPECT(pool.pin_count(metadata).value_or(1) == 0);
  EXPECT(table.validate_invariants());
  EXPECT(pool.validate_invariants());
}

void metadata_initialization_capture() {
  auto file = path();
  storage::PageManager disk(file);
  buffer::BufferPoolManager pool(disk, 4);
  storage::MutationContext context(pool);
  const auto metadata = TableHeap::create(disk, pool, schema(), &context);
  auto mutations = context.finalize();
  EXPECT(mutations.size() == 1 && mutations[0].page_id == metadata);
  verify_replay(pool, mutations);
  EXPECT(pool.pin_count(metadata).value_or(1) == 0);
}
}

int main() {
  try {
    metadata_initialization_capture();
    simple_insert_and_replay();
    new_page_link_metadata_and_delete();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  if (failures != 0) {
    std::cerr << failures << " failures\n";
    return 1;
  }
  std::cout << "All TableHeap mutation tests passed\n";
}
