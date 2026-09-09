#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace ddb::storage {
inline constexpr std::size_t kPageSize=4096;
inline constexpr std::size_t kPageHeaderSize=32;
inline constexpr std::size_t kPagePayloadSize=kPageSize-kPageHeaderSize;
inline constexpr std::uint16_t kPageFormatVersion=1;
class PageId final {public:static constexpr std::uint64_t kInvalidValue=UINT64_MAX;constexpr PageId()noexcept=default;explicit constexpr PageId(std::uint64_t v)noexcept:value_(v){}[[nodiscard]]constexpr bool is_valid()const noexcept{return value_!=kInvalidValue;}[[nodiscard]]constexpr std::uint64_t value()const noexcept{return value_;}friend constexpr bool operator==(PageId,PageId)noexcept=default;private:std::uint64_t value_{kInvalidValue};};
class Page final {public:explicit Page(PageId id={})noexcept:id_(id){clear();}[[nodiscard]]PageId id()const noexcept{return id_;}void set_id(PageId id)noexcept{id_=id;}[[nodiscard]]std::uint64_t lsn()const noexcept{return lsn_;}void set_lsn(std::uint64_t lsn)noexcept{lsn_=lsn;}[[nodiscard]]constexpr std::uint16_t version()const noexcept{return kPageFormatVersion;}[[nodiscard]]std::byte* data()noexcept{return payload_.data();}[[nodiscard]]const std::byte* data()const noexcept{return payload_.data();}[[nodiscard]]constexpr std::size_t size()const noexcept{return payload_.size();}[[nodiscard]]constexpr std::size_t payload_size()const noexcept{return payload_.size();}void clear()noexcept{std::fill(payload_.begin(),payload_.end(),std::byte{0});}private:PageId id_;std::uint64_t lsn_{0};std::array<std::byte,kPagePayloadSize> payload_{};};
}
