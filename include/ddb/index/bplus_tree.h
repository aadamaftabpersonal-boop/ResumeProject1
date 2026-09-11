#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "ddb/buffer/buffer_pool_manager.h"
#include "ddb/storage/physical_mutation.h"

namespace ddb::index {
using KeyType = std::int64_t;
struct RecordId final { ddb::storage::PageId page_id; std::uint32_t slot_id; friend bool operator==(RecordId, RecordId) = default; };
struct BPlusTreeConfig final { std::uint16_t leaf_max_keys{16}; std::uint16_t internal_max_keys{16}; };

class BPlusTree final {
 public:
  // Creates the metadata and initial empty root; returns the metadata page ID needed to reopen.
  [[nodiscard]] static ddb::storage::PageId create(ddb::storage::PageManager&, ddb::buffer::BufferPoolManager&, BPlusTreeConfig = {});
  BPlusTree(ddb::storage::PageManager&, ddb::buffer::BufferPoolManager&, ddb::storage::PageId metadata_page);

  [[nodiscard]] bool get_value(KeyType key, RecordId& result) const;
  [[nodiscard]] bool insert(KeyType key, RecordId value, ddb::storage::MutationContext* mutations = nullptr);  // false on duplicate
  [[nodiscard]] bool remove(KeyType key, ddb::storage::MutationContext* mutations = nullptr);                  // false if absent
  [[nodiscard]] std::vector<std::pair<KeyType, RecordId>> scan(KeyType lower, KeyType upper) const;
  void flush();

  [[nodiscard]] ddb::storage::PageId metadata_page_id() const noexcept { return metadata_page_id_; }
  [[nodiscard]] ddb::buffer::BufferPoolManager& buffer_pool_manager() const noexcept { return buffer_pool_; }
  [[nodiscard]] ddb::storage::PageId root_page_id() const noexcept { return root_page_id_; }
  [[nodiscard]] std::size_t height() const;
  [[nodiscard]] bool validate_invariants() const noexcept;

 private:
  struct Node;
  [[nodiscard]] Node read_node(ddb::storage::PageId) const;
  void write_node(ddb::storage::PageId, const Node&);
  [[nodiscard]] ddb::storage::PageId allocate_node(Node);
  [[nodiscard]] ddb::storage::PageId find_leaf(KeyType) const;
  void write_metadata();
  void insert_into_parent(ddb::storage::PageId left, KeyType separator, ddb::storage::PageId right);
  void rebalance_leaf(ddb::storage::PageId);
  void rebalance_internal(ddb::storage::PageId);
  void update_parent_separator(ddb::storage::PageId child, KeyType new_first);
  void set_parent(ddb::storage::PageId child, ddb::storage::PageId parent);
  [[nodiscard]] bool insert_impl(KeyType key, RecordId value);
  [[nodiscard]] bool remove_impl(KeyType key);

  ddb::storage::PageManager& page_manager_;
  ddb::buffer::BufferPoolManager& buffer_pool_;
  ddb::storage::PageId metadata_page_id_;
  ddb::storage::PageId root_page_id_;
  BPlusTreeConfig config_;
  ddb::storage::MutationContext* active_mutations_{nullptr};
};
}  // namespace ddb::index
