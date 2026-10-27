#include "arena.h"

#include <yasl/core/check.h>

namespace yasl::mem {
namespace {
constexpr size_t kMaxChunk = size_t(1) << 30;

char* align_ptr(char* p, size_t align) noexcept {
  return reinterpret_cast<char*>(
      align_up(reinterpret_cast<uintptr_t>(p), align));
}
}  // namespace

Arena::Arena(Resource* upstream, size_t first_chunk) noexcept
    : upstream_(upstream),
      next_chunk_(yasl::max(first_chunk, sizeof(Chunk) + 64)) {}

Arena::Arena(void* buffer, size_t size, Resource* upstream) noexcept
    : upstream_(upstream),
      initial_(static_cast<char*>(buffer)),
      initial_size_(size),
      next_chunk_(yasl::max<size_t>(size * 2, 4096)),
      cur_(initial_),
      end_(initial_ + size) {}

Block Arena::do_alloc(size_t size, size_t align) {
  YASL_ASSERT(is_pow2(align));
  if (size == 0) {
    size = 1;
  }
  char* p = align_ptr(cur_, align);
  if (!cur_ || p > end_ || size_t(end_ - p) < size) {
    grow(size, align);
    p = align_ptr(cur_, align);
  }
  cur_ = p + size;
  used_ += size;
  return {p, size};
}

void Arena::do_free(void* ptr, size_t size, size_t) noexcept {
  // Roll back the most recent allocation; everything else waits for release().
  if (static_cast<char*>(ptr) + size == cur_) {
    cur_ = static_cast<char*>(ptr);
    used_ -= size;
  }
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
  cur_ = static_cast<char*>(b.ptr) + header;
  end_ = static_cast<char*>(b.ptr) + b.size;
  reserved_ += b.size;
  next_chunk_ = yasl::min(yasl::max(next_chunk_, need) * 2, kMaxChunk);
}

void Arena::release() noexcept {
  while (chunks_) {
    Chunk* prev = chunks_->prev;
    upstream_->deallocate(chunks_, chunks_->size, kMaxAlign);
    chunks_ = prev;
  }
  cur_ = initial_;
  end_ = initial_ ? initial_ + initial_size_ : nullptr;
  used_ = 0;
  reserved_ = 0;
}

void Arena::reset() noexcept {
  if (!chunks_) {
    cur_ = initial_;
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
  cur_ = reinterpret_cast<char*>(keep) + align_up(sizeof(Chunk), kMaxAlign);
  end_ = reinterpret_cast<char*>(keep) + keep->size;
  used_ = 0;
  reserved_ = keep->size;
}
}  // namespace yasl::mem
