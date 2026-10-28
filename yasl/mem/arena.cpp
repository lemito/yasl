#include "arena.h"

#include <yasl/core/check.h>

namespace yasl::mem {
namespace {
constexpr size_t kMaxChunk = size_t(1) << 30;

}  // namespace

Arena::Arena(Resource* upstream, size_t first_chunk) noexcept
    : upstream_(upstream),
      next_chunk_(yasl::max(first_chunk, sizeof(Chunk) + 64)) {}

Arena::Arena(void* buffer, size_t size, Resource* upstream) noexcept
    : upstream_(upstream),
      initial_(static_cast<char*>(buffer)),
      initial_size_(size),
      next_chunk_(yasl::max<size_t>(size * 2, 4096)),
      region_(buffer, size) {}

Block Arena::do_alloc(size_t size, size_t align) {
  YASL_ASSERT(is_pow2(align));
  if (size == 0) {
    size = 1;
  }
  Block b = region_.alloc(size, align);
  if (!b.ptr) {
    grow(size, align);
    b = region_.alloc(size, align);
  }
  used_ += b.size;
  return b;
}

void Arena::do_free(void* ptr, size_t size, size_t) noexcept {
  // Roll back the most recent allocation; everything else waits for release().
  char* before = region_.cur();
  region_.free(ptr, size);
  used_ -= size_t(before - region_.cur());
}

Block Arena::do_resize(void* ptr, size_t old_size, size_t want, size_t, bool) {
  Block b = region_.resize(ptr, old_size, want);
  if (b.ptr) {
    used_ = used_ - old_size + b.size;
  }
  return b;
}

void Arena::grow(size_t size, size_t align) {
  size_t header = align_up(sizeof(Chunk), kMaxAlign);
  if (size > max_value<size_t>() - header - align) {
    throw BadAlloc();
  }
  size_t need = header + size + align;
  Block b =
      upstream_->allocate_at_least(yasl::max(next_chunk_, need), kMaxAlign);
  chunks_ = ::new (b.ptr) Chunk{chunks_, b.size};
  region_ = BumpRegion(static_cast<char*>(b.ptr) + header, b.size - header);
  next_chunk_ = yasl::min(yasl::max(next_chunk_, need) * 2, kMaxChunk);
}

size_t Arena::reserved() const noexcept {
  size_t total = 0;
  for (const Chunk* c = chunks_; c; c = c->prev) {
    total += c->size;
  }
  return total;
}

void Arena::release() noexcept {
  while (chunks_) {
    Chunk* prev = chunks_->prev;
    upstream_->deallocate(chunks_, chunks_->size, kMaxAlign);
    chunks_ = prev;
  }
  region_ = BumpRegion(initial_, initial_size_);
  used_ = 0;
}

void Arena::reset() noexcept {
  if (!chunks_) {
    region_ = BumpRegion(initial_, initial_size_);
    used_ = 0;
    return;
  }
  Chunk* keep = chunks_;
  for (Chunk* c = keep->prev; c;) {
    Chunk* prev = c->prev;
    upstream_->deallocate(c, c->size, kMaxAlign);
    c = prev;
  }
  keep->prev = nullptr;
  size_t header = align_up(sizeof(Chunk), kMaxAlign);
  region_ =
      BumpRegion(reinterpret_cast<char*>(keep) + header, keep->size - header);
  used_ = 0;
}
}  // namespace yasl::mem
