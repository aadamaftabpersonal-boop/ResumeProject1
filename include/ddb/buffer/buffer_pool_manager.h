#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ddb/storage/page_manager.h"
#include "ddb/storage/wal_durability.h"

namespace ddb::buffer {

using FrameId = std::size_t;

struct PageIdHash final {
  [[nodiscard]] std::size_t operator()(ddb::storage::PageId id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};

struct BufferPoolStats final {
  std::uint64_t fetches{0};
  std::uint64_t cache_hits{0};
  std::uint64_t disk_reads{0};
  std::uint64_t disk_writes{0};
};

// A fixed-size, single-threaded cache of persistent pages.
class BufferPoolManager final {
 public:
  BufferPoolManager(ddb::storage::PageManager& page_manager, std::size_t capacity,
                    ddb::storage::WalDurabilityProvider* wal_durability = nullptr);
  ~BufferPoolManager() noexcept;

  BufferPoolManager(const BufferPoolManager&) = delete;
  BufferPoolManager& operator=(const BufferPoolManager&) = delete;
  BufferPoolManager(BufferPoolManager&&) = delete;
  BufferPoolManager& operator=(BufferPoolManager&&) = delete;

  // Returns nullptr for an invalid ID, pin-count overflow, or no evictable frame.
  // Lower-level storage read errors are propagated as StorageError.
  [[nodiscard]] ddb::storage::Page* fetch_page(ddb::storage::PageId id);
  [[nodiscard]] bool unpin_page(ddb::storage::PageId id, bool is_dirty);
  [[nodiscard]] bool flush_page(ddb::storage::PageId id);
  // Removes only the cached copy. A non-cached page is already absent and succeeds.
  [[nodiscard]] bool delete_page(ddb::storage::PageId id);
  void flush_all_pages();

  [[nodiscard]] std::size_t capacity() const noexcept { return frames_.size(); }
  [[nodiscard]] std::size_t cached_page_count() const noexcept { return page_table_.size(); }
  [[nodiscard]] bool contains(ddb::storage::PageId id) const noexcept;
  [[nodiscard]] std::optional<std::uint32_t> pin_count(ddb::storage::PageId id) const noexcept;
  [[nodiscard]] std::optional<bool> is_dirty(ddb::storage::PageId id) const noexcept;
  // Captured transactional mutations make a page ineligible for every
  // write-back path until a future coordinator gives the mutation an LSN.
  void mark_mutation_pending(ddb::storage::PageId id);
  void finalize_pending_mutation(ddb::storage::PageId id, std::uint64_t lsn);
  [[nodiscard]] std::optional<std::uint32_t> pending_mutation_count(ddb::storage::PageId id) const noexcept;
  [[nodiscard]] const BufferPoolStats& stats() const noexcept { return stats_; }
  [[nodiscard]] bool validate_invariants() const noexcept;
  void set_wal_durability_provider(ddb::storage::WalDurabilityProvider* provider) noexcept { wal_durability_ = provider; }

 private:
  struct Frame final {
    ddb::storage::Page page{};
    std::uint32_t pin_count{0};
    bool dirty{false};
    bool occupied{false};
    std::uint32_t pending_mutations{0};
  };

  [[nodiscard]] std::optional<FrameId> acquire_frame();
  void make_evictable(FrameId frame_id);
  void remove_from_lru(FrameId frame_id);
  void reset_frame(FrameId frame_id) noexcept;
  void write_dirty_frame(FrameId frame_id);
  void require_wal_durable(const Frame& frame) const;
  void require_mutations_finalized(const Frame& frame) const;

  ddb::storage::PageManager& page_manager_;
  std::vector<Frame> frames_;
  std::unordered_map<ddb::storage::PageId, FrameId, PageIdHash> page_table_;
  std::vector<FrameId> free_frames_;
  std::list<FrameId> lru_;
  std::unordered_map<FrameId, std::list<FrameId>::iterator> lru_positions_;
  BufferPoolStats stats_{};
  ddb::storage::WalDurabilityProvider* wal_durability_{nullptr};
};
}  // namespace ddb::buffer
