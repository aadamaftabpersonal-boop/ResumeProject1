#include "ddb/buffer/buffer_pool_manager.h"

#include <functional>
#include <limits>
#include <stdexcept>

namespace ddb::buffer {

BufferPoolManager::BufferPoolManager(ddb::storage::PageManager& page_manager, std::size_t capacity)
    : page_manager_(page_manager), frames_(capacity) {
  free_frames_.reserve(capacity);
  for (FrameId id = 0; id < capacity; ++id) {
    free_frames_.push_back(capacity - id - 1U);
  }
}

BufferPoolManager::~BufferPoolManager() noexcept {
  try {
    flush_all_pages();
  } catch (...) {
    // Destructors cannot safely report I/O failures; callers needing that guarantee use flush_all_pages().
  }
}

bool BufferPoolManager::contains(ddb::storage::PageId id) const noexcept {
  return page_table_.contains(id);
}

std::optional<std::uint32_t> BufferPoolManager::pin_count(ddb::storage::PageId id) const noexcept {
  const auto entry = page_table_.find(id);
  if (entry == page_table_.end()) return std::nullopt;
  return frames_[entry->second].pin_count;
}

std::optional<bool> BufferPoolManager::is_dirty(ddb::storage::PageId id) const noexcept {
  const auto entry = page_table_.find(id);
  if (entry == page_table_.end()) return std::nullopt;
  return frames_[entry->second].dirty;
}

void BufferPoolManager::remove_from_lru(FrameId frame_id) {
  const auto position = lru_positions_.find(frame_id);
  if (position != lru_positions_.end()) {
    lru_.erase(position->second);
    lru_positions_.erase(position);
  }
}

void BufferPoolManager::make_evictable(FrameId frame_id) {
  remove_from_lru(frame_id);
  lru_.push_back(frame_id);
  lru_positions_.emplace(frame_id, std::prev(lru_.end()));
}

void BufferPoolManager::reset_frame(FrameId frame_id) noexcept {
  Frame& frame = frames_[frame_id];
  frame.page = ddb::storage::Page{};
  frame.pin_count = 0;
  frame.dirty = false;
  frame.occupied = false;
}

void BufferPoolManager::write_dirty_frame(FrameId frame_id) {
  Frame& frame = frames_[frame_id];
  if (frame.dirty) {
    page_manager_.write_page(frame.page.id(), frame.page);
    ++stats_.disk_writes;
    frame.dirty = false;
  }
}

std::optional<FrameId> BufferPoolManager::acquire_frame() {
  if (!free_frames_.empty()) {
    const FrameId frame_id = free_frames_.back();
    free_frames_.pop_back();
    return frame_id;
  }
  if (lru_.empty()) return std::nullopt;

  const FrameId victim = lru_.front();
  Frame& frame = frames_[victim];
  // Do not detach the victim until its write-back succeeds: a failed write must leave
  // the frame reachable and evictable instead of corrupting cache bookkeeping.
  write_dirty_frame(victim);
  lru_.pop_front();
  lru_positions_.erase(victim);
  page_table_.erase(frame.page.id());
  reset_frame(victim);
  return victim;
}

ddb::storage::Page* BufferPoolManager::fetch_page(ddb::storage::PageId id) {
  ++stats_.fetches;
  const auto existing = page_table_.find(id);
  if (existing != page_table_.end()) {
    Frame& frame = frames_[existing->second];
    if (frame.pin_count == std::numeric_limits<std::uint32_t>::max()) return nullptr;
    ++frame.pin_count;
    remove_from_lru(existing->second);
    ++stats_.cache_hits;
    return &frame.page;
  }
  if (!id.is_valid()) return nullptr;

  const auto frame_id = acquire_frame();
  if (!frame_id.has_value()) return nullptr;
  Frame& frame = frames_[*frame_id];
  try {
    page_manager_.read_page(id, frame.page);
    ++stats_.disk_reads;
  } catch (...) {
    reset_frame(*frame_id);
    free_frames_.push_back(*frame_id);
    throw;
  }
  frame.pin_count = 1;
  frame.dirty = false;
  frame.occupied = true;
  try {
    const auto [entry, inserted] = page_table_.emplace(id, *frame_id);
    (void)entry;
    if (!inserted) throw std::logic_error("duplicate page-table insertion");
  } catch (...) {
    reset_frame(*frame_id);
    free_frames_.push_back(*frame_id);
    throw;
  }
  return &frame.page;
}

bool BufferPoolManager::unpin_page(ddb::storage::PageId id, bool is_dirty) {
  const auto entry = page_table_.find(id);
  if (entry == page_table_.end()) return false;
  Frame& frame = frames_[entry->second];
  if (frame.pin_count == 0) return false;
  --frame.pin_count;
  frame.dirty = frame.dirty || is_dirty;
  if (frame.pin_count == 0) make_evictable(entry->second);
  return true;
}

bool BufferPoolManager::flush_page(ddb::storage::PageId id) {
  const auto entry = page_table_.find(id);
  if (entry == page_table_.end()) return false;
  Frame& frame = frames_[entry->second];
  page_manager_.write_page(id, frame.page);
  ++stats_.disk_writes;
  page_manager_.flush();
  frame.dirty = false;
  return true;
}

bool BufferPoolManager::delete_page(ddb::storage::PageId id) {
  const auto entry = page_table_.find(id);
  if (entry == page_table_.end()) return true;
  Frame& frame = frames_[entry->second];
  if (frame.pin_count != 0) return false;
  const FrameId frame_id = entry->second;
  write_dirty_frame(frame_id);
  remove_from_lru(frame_id);
  page_table_.erase(entry);
  reset_frame(frame_id);
  free_frames_.push_back(frame_id);
  return true;
}

void BufferPoolManager::flush_all_pages() {
  for (FrameId frame_id = 0; frame_id < frames_.size(); ++frame_id) {
    Frame& frame = frames_[frame_id];
    if (frame.occupied && frame.dirty) {
      page_manager_.write_page(frame.page.id(), frame.page);
      ++stats_.disk_writes;
    }
  }
  page_manager_.flush();
  for (Frame& frame : frames_) {
    if (frame.occupied) frame.dirty = false;
  }
}

bool BufferPoolManager::validate_invariants() const noexcept {
  if (page_table_.size() + free_frames_.size() != frames_.size()) return false;
  for (const auto& [page_id, frame_id] : page_table_) {
    if (frame_id >= frames_.size()) return false;
    const Frame& frame = frames_[frame_id];
    if (!frame.occupied || frame.page.id() != page_id) return false;
    const bool in_lru = lru_positions_.contains(frame_id);
    if ((frame.pin_count == 0) != in_lru) return false;
  }
  for (const FrameId frame_id : free_frames_) {
    if (frame_id >= frames_.size() || frames_[frame_id].occupied || lru_positions_.contains(frame_id)) return false;
  }
  if (lru_.size() != lru_positions_.size()) return false;
  for (const FrameId frame_id : lru_) {
    if (frame_id >= frames_.size() || !frames_[frame_id].occupied || frames_[frame_id].pin_count != 0) return false;
  }
  return true;
}
}  // namespace ddb::buffer
