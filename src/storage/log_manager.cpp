#include "ddb/storage/log_manager.h"

#include <array>
#include <cstring>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ddb::storage {
namespace {
constexpr std::array<std::byte, 4> kMagic{std::byte{'D'}, std::byte{'W'}, std::byte{'A'}, std::byte{'L'}};
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 36;
constexpr std::size_t kChecksumSize = 4;
constexpr std::size_t kMinimumRecordSize = kHeaderSize + kChecksumSize;
constexpr std::uint32_t kMaxRecordSize = 32U * 1024U * 1024U;

void put16(std::vector<std::byte>& out, std::uint16_t value) { out.push_back(std::byte(value & 0xffU)); out.push_back(std::byte(value >> 8U)); }
void put32(std::vector<std::byte>& out, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) out.push_back(std::byte((value >> (i * 8U)) & 0xffU)); }
void put64(std::vector<std::byte>& out, std::uint64_t value) { for (unsigned i = 0; i < 8; ++i) out.push_back(std::byte((value >> (i * 8U)) & 0xffU)); }
std::uint16_t get16(const std::byte* in) { return std::uint16_t(std::to_integer<unsigned char>(in[0])) | (std::uint16_t(std::to_integer<unsigned char>(in[1])) << 8U); }
std::uint32_t get32(const std::byte* in) { std::uint32_t value{}; for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(std::to_integer<unsigned char>(in[i])) << (i * 8U); return value; }
std::uint64_t get64(const std::byte* in) { std::uint64_t value{}; for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(std::to_integer<unsigned char>(in[i])) << (i * 8U); return value; }
std::uint32_t checksum(const std::vector<std::byte>& bytes) { std::uint32_t value = 2166136261U; for (const auto byte : bytes) { value ^= std::to_integer<unsigned char>(byte); value *= 16777619U; } return value; }
void require(bool condition, const char* message) { if (!condition) throw WalError(message); }
}

LogManager::LogManager(const std::filesystem::path& path) : path_(path) { open_and_scan(); }
LogManager::~LogManager() noexcept { try { flush(); } catch (...) {} }

std::vector<std::byte> LogManager::encode(const WalRecord& record) {
  require(record.lsn != 0, "WAL LSN must be nonzero");
  require(record.type >= WalRecordType::Begin && record.type <= WalRecordType::PageAllocate, "invalid WAL record type");
  require(record.payload.size() <= kMaxRecordSize - kMinimumRecordSize, "WAL payload is too large");
  const auto length = static_cast<std::uint32_t>(kMinimumRecordSize + record.payload.size());
  std::vector<std::byte> out; out.reserve(length);
  out.insert(out.end(), kMagic.begin(), kMagic.end()); put16(out, kVersion); put16(out, static_cast<std::uint16_t>(kHeaderSize));
  put32(out, length); put64(out, record.lsn); put64(out, record.transaction_id); out.push_back(std::byte(static_cast<std::uint8_t>(record.type)));
  out.insert(out.end(), 3, std::byte{0}); put32(out, static_cast<std::uint32_t>(record.payload.size())); out.insert(out.end(), record.payload.begin(), record.payload.end());
  put32(out, checksum(out)); return out;
}

WalRecord LogManager::decode(const std::vector<std::byte>& bytes) {
  require(bytes.size() >= kMinimumRecordSize, "truncated WAL record");
  require(std::equal(kMagic.begin(), kMagic.end(), bytes.begin()), "invalid WAL magic");
  require(get16(bytes.data() + 4) == kVersion, "unsupported WAL version");
  require(get16(bytes.data() + 6) == kHeaderSize, "invalid WAL header length");
  const auto length = get32(bytes.data() + 8); const auto payload_length = get32(bytes.data() + 32);
  require(length == bytes.size() && length >= kMinimumRecordSize && length <= kMaxRecordSize, "invalid WAL record length");
  require(payload_length == length - kMinimumRecordSize, "invalid WAL payload length");
  const auto saved_checksum = get32(bytes.data() + bytes.size() - kChecksumSize);
  std::vector<std::byte> checked(bytes.begin(), bytes.end() - static_cast<std::ptrdiff_t>(kChecksumSize));
  require(checksum(checked) == saved_checksum, "invalid WAL checksum");
  const auto raw_type = std::to_integer<std::uint8_t>(bytes[28]);
  require(raw_type >= static_cast<std::uint8_t>(WalRecordType::Begin) && raw_type <= static_cast<std::uint8_t>(WalRecordType::PageAllocate), "invalid WAL record type");
  return {get64(bytes.data() + 12), get64(bytes.data() + 20), static_cast<WalRecordType>(raw_type),
      std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), bytes.end() - static_cast<std::ptrdiff_t>(kChecksumSize))};
}

std::vector<std::byte> LogManager::encode_physical_mutation(const PhysicalMutation& mutation) {
  require(mutation.page_id.is_valid(), "invalid WAL mutation page id");
  require(mutation.before_image.size() == kPagePayloadSize && mutation.after_image.size() == kPagePayloadSize, "invalid WAL mutation image size");
  std::vector<std::byte> out; out.reserve(8 + 1 + 8 + 8 + mutation.before_image.size() + mutation.after_image.size());
  put64(out, mutation.page_id.value()); out.push_back(std::byte(static_cast<std::uint8_t>(mutation.kind))); put64(out, mutation.sequence);
  put32(out, static_cast<std::uint32_t>(mutation.before_image.size())); put32(out, static_cast<std::uint32_t>(mutation.after_image.size()));
  out.insert(out.end(), mutation.before_image.begin(), mutation.before_image.end()); out.insert(out.end(), mutation.after_image.begin(), mutation.after_image.end()); return out;
}

PhysicalMutation LogManager::decode_physical_mutation(const std::vector<std::byte>& bytes) {
  require(bytes.size() >= 25, "truncated WAL mutation payload"); const auto before_length = get32(bytes.data() + 17); const auto after_length = get32(bytes.data() + 21);
  require(before_length == kPagePayloadSize && after_length == kPagePayloadSize && bytes.size() == 25ULL + before_length + after_length, "invalid WAL mutation image lengths");
  const auto kind = std::to_integer<std::uint8_t>(bytes[8]); require(kind == static_cast<std::uint8_t>(MutationKind::PayloadWrite), "invalid WAL mutation kind");
  const auto page = PageId(get64(bytes.data())); require(page.is_valid(), "invalid WAL mutation page id");
  const auto before_begin = bytes.begin() + 25; const auto after_begin = before_begin + before_length;
  return {page, static_cast<MutationKind>(kind), get64(bytes.data() + 9), {before_begin, after_begin}, {after_begin, bytes.end()}};
}

void LogManager::open_and_scan() {
  std::error_code error; if (!std::filesystem::exists(path_, error)) { std::ofstream create(path_, std::ios::binary); if (!create) throw WalError("cannot create WAL file"); }
  if (error) throw WalError("cannot inspect WAL file: " + error.message());
  file_.open(path_, std::ios::binary | std::ios::in | std::ios::out); if (!file_) throw WalError("cannot open WAL file");
  const auto records_on_disk = records(); if (!records_on_disk.empty()) next_lsn_ = records_on_disk.back().lsn + 1; durable_lsn_ = records_on_disk.empty() ? 0 : records_on_disk.back().lsn;
  std::uintmax_t valid_bytes{}; for (const auto& record : records_on_disk) valid_bytes += encode(record).size();
  const auto actual_bytes = std::filesystem::file_size(path_, error); if (error) throw WalError("cannot determine WAL size: " + error.message());
  if (actual_bytes != valid_bytes) { file_.close(); std::filesystem::resize_file(path_, valid_bytes, error); if (error) throw WalError("cannot discard truncated WAL tail: " + error.message()); file_.open(path_, std::ios::binary | std::ios::in | std::ios::out); if (!file_) throw WalError("cannot reopen WAL after truncating tail"); }
  file_.clear(); file_.seekp(0, std::ios::end); if (!file_) throw WalError("cannot seek WAL append position");
}

std::uint64_t LogManager::append(WalRecordType type, std::uint64_t transaction_id, std::vector<std::byte> payload) {
  std::lock_guard guard(mutex_); WalRecord record{next_lsn_++, transaction_id, type, std::move(payload)}; const auto encoded = encode(record);
  file_.clear(); file_.seekp(0, std::ios::end); file_.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())); file_.flush(); if (!file_) throw WalError("WAL append failed"); return record.lsn;
}
std::uint64_t LogManager::append_begin(std::uint64_t id) { return append(WalRecordType::Begin, id); }
std::uint64_t LogManager::append_physical_mutation(std::uint64_t id, const PhysicalMutation& mutation) { return append(WalRecordType::PhysicalMutation, id, encode_physical_mutation(mutation)); }
std::uint64_t LogManager::append_commit(std::uint64_t id) { return append(WalRecordType::Commit, id); }
std::uint64_t LogManager::append_abort(std::uint64_t id) { return append(WalRecordType::Abort, id); }
std::vector<std::byte> LogManager::encode_page_allocate(PageId page) { require(page.is_valid(), "invalid WAL allocation page id"); std::vector<std::byte> out; out.reserve(8); put64(out, page.value()); return out; }
PageId LogManager::decode_page_allocate(const std::vector<std::byte>& bytes) { require(bytes.size()==8, "invalid WAL allocation payload length"); const PageId page(get64(bytes.data())); require(page.is_valid(), "invalid WAL allocation page id"); return page; }
std::uint64_t LogManager::append_page_allocate(std::uint64_t id, PageId page) { return append(WalRecordType::PageAllocate, id, encode_page_allocate(page)); }
void LogManager::log_begin(ddb::concurrency::TransactionId id) { (void)append_begin(id); }
std::uint64_t LogManager::log_physical_mutation(ddb::concurrency::TransactionId id, const PhysicalMutation& mutation) { return append_physical_mutation(id, mutation); }
void LogManager::prepare_commit(ddb::concurrency::Transaction& transaction) {
  if (transaction.has_unfinalized_mutations()) throw WalError("cannot commit transaction with unlogged mutations");
  (void)append_commit(transaction.id()); flush();
}
void LogManager::prepare_abort(ddb::concurrency::Transaction& transaction) {
  if (transaction.has_unresolved_mutations_for_abort()) throw WalError("physical abort undo is not implemented");
  (void)append_abort(transaction.id()); flush();
}
void LogManager::flush() {
  std::lock_guard guard(mutex_); file_.flush(); if (!file_) throw WalError("WAL flush failed");
#ifdef _WIN32
  const HANDLE handle = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw WalError("cannot open WAL for durable flush");
  const BOOL flushed = FlushFileBuffers(handle); const DWORD error = flushed ? ERROR_SUCCESS : GetLastError(); CloseHandle(handle);
  if (!flushed) throw WalError("durable WAL flush failed: " + std::to_string(error));
#endif
  durable_lsn_ = next_lsn_ - 1;
}
std::uint64_t LogManager::durable_lsn() const noexcept { std::lock_guard guard(mutex_); return durable_lsn_; }
std::uint64_t LogManager::next_lsn() const noexcept { std::lock_guard guard(mutex_); return next_lsn_; }

std::vector<WalRecord> LogManager::records() const {
  std::lock_guard guard(mutex_); std::ifstream input(path_, std::ios::binary); if (!input) throw WalError("cannot read WAL file");
  std::vector<WalRecord> out; for (;;) { std::array<std::byte, kHeaderSize> header{}; input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())); const auto count = input.gcount(); if (count == 0) break; if (count != static_cast<std::streamsize>(header.size())) break;
    const auto length = get32(header.data() + 8); if (length < kMinimumRecordSize || length > kMaxRecordSize) throw WalError("invalid WAL record length"); std::vector<std::byte> raw(header.begin(), header.end()); raw.resize(length); input.read(reinterpret_cast<char*>(raw.data() + kHeaderSize), static_cast<std::streamsize>(length - kHeaderSize)); if (input.gcount() != static_cast<std::streamsize>(length - kHeaderSize)) break;
    auto record = decode(raw); if (!out.empty() && record.lsn <= out.back().lsn) throw WalError("non-monotonic WAL LSN"); out.push_back(std::move(record)); }
  return out;
}
}  // namespace ddb::storage
