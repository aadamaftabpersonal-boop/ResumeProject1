#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "ddb/buffer/buffer_pool_manager.h"
namespace ddb::storage {
enum class MutationKind : std::uint8_t { PayloadWrite };
struct PhysicalMutation final { PageId page_id; MutationKind kind{MutationKind::PayloadWrite}; std::uint64_t sequence{}; std::vector<std::byte> before_image; std::vector<std::byte> after_image; };
// A future WAL finalizer persists a captured mutation and returns its LSN. A
// null result transfers ownership to a transaction and blocks page write-back
// until that transaction subsequently supplies an LSN.
class MutationFinalizer {
 public:
  virtual ~MutationFinalizer() = default;
  [[nodiscard]] virtual std::optional<std::uint64_t> finalize_mutation(PhysicalMutation mutation) = 0;
};
class MutationContext final { public: explicit MutationContext(ddb::buffer::BufferPoolManager&, MutationFinalizer* = nullptr); void watch(PageId); void finish(PageId); void finish(PageId, Page&); [[nodiscard]] bool has_unfinished_capture() const noexcept { return !pending_.empty(); } [[nodiscard]] std::vector<PhysicalMutation> finalize(); [[nodiscard]] std::vector<PhysicalMutation> drain_captured(); private: struct Pending {PageId id;std::vector<std::byte> before;}; void capture(PageId, const std::byte*, std::size_t, Page*); ddb::buffer::BufferPoolManager& pool_;MutationFinalizer* finalizer_;std::vector<Pending> pending_;std::vector<PhysicalMutation> mutations_;std::uint64_t next_sequence_{}; };
void apply_after_image(ddb::buffer::BufferPoolManager&,const PhysicalMutation&);void apply_before_image(ddb::buffer::BufferPoolManager&,const PhysicalMutation&);
}
