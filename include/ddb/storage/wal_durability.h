#pragma once

#include <cstdint>
#include <stdexcept>

namespace ddb::storage {

// Implemented by the future WAL.  The buffer pool deliberately depends only on
// this durability boundary, never on a concrete log implementation.
class WalDurabilityProvider {
 public:
  virtual ~WalDurabilityProvider() = default;
  [[nodiscard]] virtual std::uint64_t durable_lsn() const noexcept = 0;
};

class WalDurabilityError final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace ddb::storage
