#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ddb::storage {

inline constexpr std::size_t kPageSize = 4096;

class PageId final {
 public:
  static constexpr std::uint64_t kInvalidValue = UINT64_MAX;

  constexpr PageId() noexcept = default;
  explicit constexpr PageId(std::uint64_t value) noexcept : value_(value) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != kInvalidValue; }
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
  friend constexpr bool operator==(PageId, PageId) noexcept = default;

 private:
  std::uint64_t value_{kInvalidValue};
};

class Page final {
 public:
  explicit Page(PageId id = {}) noexcept : id_(id) { clear(); }

  [[nodiscard]] PageId id() const noexcept { return id_; }
  void set_id(PageId id) noexcept { id_ = id; }

  [[nodiscard]] std::byte* data() noexcept { return data_.data(); }
  [[nodiscard]] const std::byte* data() const noexcept { return data_.data(); }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return data_.size(); }

  void clear() noexcept { std::fill(data_.begin(), data_.end(), std::byte{0}); }

 private:
  PageId id_;
  std::array<std::byte, kPageSize> data_{};
};

static_assert(sizeof(std::array<std::byte, kPageSize>) == kPageSize,
              "Page data must occupy exactly 4096 bytes");
}  // namespace ddb::storage
