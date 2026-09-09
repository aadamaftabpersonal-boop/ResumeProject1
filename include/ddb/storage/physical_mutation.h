#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "ddb/buffer/buffer_pool_manager.h"
namespace ddb::storage {
enum class MutationKind : std::uint8_t { PayloadWrite };
struct PhysicalMutation final { PageId page_id; MutationKind kind{MutationKind::PayloadWrite}; std::uint64_t sequence{}; std::vector<std::byte> before_image; std::vector<std::byte> after_image; };
class MutationContext final { public: explicit MutationContext(ddb::buffer::BufferPoolManager&); void watch(PageId); void finish(PageId); [[nodiscard]] std::vector<PhysicalMutation> finalize(); private: struct Pending {PageId id;std::vector<std::byte> before;}; ddb::buffer::BufferPoolManager& pool_;std::vector<Pending> pending_;std::vector<PhysicalMutation> mutations_;std::uint64_t next_sequence_{}; };
void apply_after_image(ddb::buffer::BufferPoolManager&,const PhysicalMutation&);void apply_before_image(ddb::buffer::BufferPoolManager&,const PhysicalMutation&);
}
