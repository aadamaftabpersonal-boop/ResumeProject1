#include "ddb/storage/physical_mutation.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ddb::storage {
namespace {
std::vector<std::byte> image(ddb::buffer::BufferPoolManager& pool, PageId id) {
  auto* page = pool.fetch_page(id);
  if (!page) throw std::runtime_error("cannot fetch mutation page");
  std::vector<std::byte> out(page->data(), page->data() + page->payload_size());
  if (!pool.unpin_page(id, false)) throw std::runtime_error("cannot unpin mutation page");
  return out;
}

void apply(ddb::buffer::BufferPoolManager& pool, const PhysicalMutation& mutation,
           const std::vector<std::byte>& bytes) {
  if (bytes.size() != kPagePayloadSize) throw std::invalid_argument("invalid mutation payload image");
  auto* page = pool.fetch_page(mutation.page_id);
  if (!page) throw std::runtime_error("cannot fetch replay page");
  std::memcpy(page->data(), bytes.data(), bytes.size());
  if (!pool.unpin_page(mutation.page_id, true)) throw std::runtime_error("cannot unpin replay page");
}
}  // namespace

MutationContext::MutationContext(ddb::buffer::BufferPoolManager& pool, MutationFinalizer* finalizer)
    : pool_(pool), finalizer_(finalizer) {}

void MutationContext::watch(PageId id) {
  if (!id.is_valid()) throw std::invalid_argument("invalid mutation page");
  pending_.push_back({id, image(pool_, id)});
}

void MutationContext::capture(PageId id, const std::byte* after, std::size_t size, Page* page) {
  auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Pending& pending) { return pending.id == id; });
  if (it == pending_.end()) throw std::logic_error("finish without watch");
  std::vector<std::byte> after_image(after, after + size);
  if (after_image != it->before) {
    PhysicalMutation mutation{id, MutationKind::PayloadWrite, next_sequence_++, std::move(it->before), std::move(after_image)};
    if (finalizer_ == nullptr) {
      mutations_.push_back(std::move(mutation));
    } else {
      if (page == nullptr) throw std::logic_error("transactional mutation must finish while the page is pinned");
      const auto lsn = finalizer_->finalize_mutation(std::move(mutation));
      if (lsn.has_value()) {
        if (*lsn == 0) throw std::invalid_argument("a finalized mutation requires a nonzero LSN");
        page->set_lsn(*lsn);
      } else {
        pool_.mark_mutation_pending(id);
      }
    }
  }
  pending_.erase(it);
}

void MutationContext::finish(PageId id) {
  if (finalizer_ != nullptr) throw std::logic_error("transactional mutation must finish while the page is pinned");
  auto* page = pool_.fetch_page(id);
  if (!page) throw std::runtime_error("cannot fetch mutation page");
  try { capture(id, page->data(), page->payload_size(), nullptr); }
  catch (...) { (void)pool_.unpin_page(id, false); throw; }
  if (!pool_.unpin_page(id, false)) throw std::runtime_error("cannot unpin mutation page");
}

void MutationContext::finish(PageId id, Page& page) {
  if (page.id() != id) throw std::invalid_argument("mutation page does not match finish target");
  capture(id, page.data(), page.payload_size(), &page);
}

std::vector<PhysicalMutation> MutationContext::drain_captured() {
  if (!pending_.empty()) throw std::logic_error("unfinished mutation capture");
  auto out = std::move(mutations_);
  mutations_.clear();
  return out;
}

std::vector<PhysicalMutation> MutationContext::finalize() { return drain_captured(); }
void apply_after_image(ddb::buffer::BufferPoolManager& pool, const PhysicalMutation& mutation) { apply(pool, mutation, mutation.after_image); }
void apply_before_image(ddb::buffer::BufferPoolManager& pool, const PhysicalMutation& mutation) { apply(pool, mutation, mutation.before_image); }
}  // namespace ddb::storage
