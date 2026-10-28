#include "pool.h"

#include <yasl/core/check.h>

namespace yasl::mem {
Pool::Pool(Resource* upstream, Options opts) noexcept
    : upstream_(upstream), opts_(opts) {
  size_t largest = kMinBlock << (kMaxClasses - 1);
  opts_.max_block = bit_ceil(yasl::clamp(opts_.max_block, kMinBlock, largest));
}

size_t Pool::class_of(size_t size, size_t align) const noexcept {
  if (align > kMaxAlign) {
    return kNoClass;
  }
  size_t need = yasl::max(yasl::max(size, align), kMinBlock);
  if (need > opts_.max_block) {
    return kNoClass;
  }
  return size_t(bit_width(uint64(need - 1))) - 3;
}

Block Pool::do_alloc(size_t size, size_t align) {
  YASL_ASSERT(is_pow2(align));
  size_t idx = class_of(size, align);
  if (idx == kNoClass) {
    return upstream_->allocate_at_least(size, align);
  }
  size_t block = kMinBlock << idx;
  if (FreeNode* n = free_[idx]) {
    free_[idx] = n->next;
    return {n, block};
  }
  // Blocks are aligned to their size (up to kMaxAlign).
  size_t balign = block < kMaxAlign ? block : kMaxAlign;
  auto fits = [&] {
    return cur_ &&
           size_t(end_ - cur_) >=
               block + (align_up(reinterpret_cast<uintptr_t>(cur_), balign) -
                        reinterpret_cast<uintptr_t>(cur_));
  };
  if (!fits()) {
    refill(block);
  }
  cur_ = reinterpret_cast<char*>(
      align_up(reinterpret_cast<uintptr_t>(cur_), balign));
  void* p = cur_;
  cur_ += block;
  return {p, block};
}

void Pool::do_free(void* ptr, size_t size, size_t align) noexcept {
  size_t idx = class_of(size, align);
  if (idx == kNoClass) {
    upstream_->deallocate(ptr, size, align);
    return;
  }
  free_[idx] = ::new (ptr) FreeNode{free_[idx]};
}

void Pool::refill(size_t block) {
  size_t header = align_up(sizeof(Slab), kMaxAlign);
  size_t want = yasl::max(opts_.slab_size, header + block * 4);
  Block b = upstream_->allocate_at_least(want, kMaxAlign);
  slabs_ = ::new (b.ptr) Slab{slabs_, b.size};
  cur_ = static_cast<char*>(b.ptr) + header;
  end_ = static_cast<char*>(b.ptr) + b.size;
}

void Pool::release() noexcept {
  while (slabs_) {
    Slab* next = slabs_->next;
    upstream_->deallocate(slabs_, slabs_->size, kMaxAlign);
    slabs_ = next;
  }
  cur_ = end_ = nullptr;
  for (FreeNode*& f : free_) {
    f = nullptr;
  }
}
}  // namespace yasl::mem
