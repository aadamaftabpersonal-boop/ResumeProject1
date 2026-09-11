#pragma once
#include <optional>
#include "ddb/execution/tuple.h"
#include "ddb/buffer/buffer_pool_manager.h"
#include "ddb/storage/physical_mutation.h"
namespace ddb::execution {
class TableHeap final {
 public:
  static ddb::storage::PageId create(ddb::storage::PageManager&, ddb::buffer::BufferPoolManager&, const Schema&, ddb::storage::MutationContext* mutations = nullptr);
  TableHeap(ddb::storage::PageManager&, ddb::buffer::BufferPoolManager&, ddb::storage::PageId metadata, Schema schema);
  [[nodiscard]] std::optional<RecordId> insert(const Tuple& tuple, ddb::storage::MutationContext* mutations = nullptr);
  [[nodiscard]] std::optional<Tuple> get(RecordId) const;
  [[nodiscard]] bool erase(RecordId, ddb::storage::MutationContext* mutations = nullptr);
  [[nodiscard]] std::vector<std::pair<RecordId,Tuple>> scan() const;
  [[nodiscard]] const Schema& schema() const noexcept { return schema_; }
  [[nodiscard]] ddb::storage::PageId metadata_page_id() const noexcept { return meta_; }
  [[nodiscard]] ddb::buffer::BufferPoolManager& buffer_pool_manager() const noexcept { return bp_; }
  [[nodiscard]] bool validate_invariants() const;
  void flush();
 private:
  [[nodiscard]] std::vector<std::byte> serialize(const Tuple&) const;
  [[nodiscard]] std::optional<Tuple> deserialize(const std::byte*,std::size_t) const;
  void write_metadata();
  [[nodiscard]] std::optional<RecordId> insert_impl(const Tuple&);
  [[nodiscard]] bool erase_impl(RecordId);
  ddb::storage::PageManager& pm_; ddb::buffer::BufferPoolManager& bp_; ddb::storage::PageId meta_; Schema schema_; ddb::storage::PageId first_{}; ddb::storage::PageId last_{};
  ddb::storage::MutationContext* active_mutations_{nullptr};
};
}
