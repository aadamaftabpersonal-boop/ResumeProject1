#pragma once
#include <optional>
#include "ddb/execution/tuple.h"
#include "ddb/buffer/buffer_pool_manager.h"
namespace ddb::execution {
class TableHeap final {
 public:
  static ddb::storage::PageId create(ddb::storage::PageManager&, ddb::buffer::BufferPoolManager&, const Schema&);
  TableHeap(ddb::storage::PageManager&, ddb::buffer::BufferPoolManager&, ddb::storage::PageId metadata, Schema schema);
  [[nodiscard]] std::optional<RecordId> insert(const Tuple& tuple);
  [[nodiscard]] std::optional<Tuple> get(RecordId) const;
  [[nodiscard]] bool erase(RecordId);
  [[nodiscard]] std::vector<std::pair<RecordId,Tuple>> scan() const;
  [[nodiscard]] const Schema& schema() const noexcept { return schema_; }
  [[nodiscard]] ddb::storage::PageId metadata_page_id() const noexcept { return meta_; }
  [[nodiscard]] bool validate_invariants() const;
  void flush();
 private:
  [[nodiscard]] std::vector<std::byte> serialize(const Tuple&) const;
  [[nodiscard]] std::optional<Tuple> deserialize(const std::byte*,std::size_t) const;
  void write_metadata();
  ddb::storage::PageManager& pm_; ddb::buffer::BufferPoolManager& bp_; ddb::storage::PageId meta_; Schema schema_; ddb::storage::PageId first_{}; ddb::storage::PageId last_{};
};
}
