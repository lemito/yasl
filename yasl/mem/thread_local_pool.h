#pragma once

#include "compose.h"

namespace yasl::mem {
// Small-block allocator with one heap per thread; a composition block, see
// compose.h.
//
//   * sizes up to kMaxSmall are rounded up to one of 28 classes (16-byte steps
//     to 128, then four classes per power of two: at most 25% waste);
//   * blocks of a class come from 64 KiB slabs aligned to their size, so a
//     block finds its slab (and the slab header: owner, class, free lists) by
//     masking the address;
//   * the owner thread allocates and frees without synchronisation; a free from
//     another thread is pushed on the slab's lock-free list and picked up by
//     the owner on its next allocation of that class;
//   * a thread that exits hands its slabs to a shared heap, which serves late
//     allocations and is swept on every thread exit; empty slabs go back to
//     mem::sys (one empty slab per class stays cached).
//
// The block routing assumes size <= kMaxSmall and align <= kMaxAlign. Other
// requests return {}; combine with AlignSplit/Segregator (DefaultChain does).
// owns() cannot tell small blocks from others and always says yes.
class ThreadLocalPool {
 public:
  static constexpr size_t kMaxSmall = 4096;
  static constexpr size_t kSlabSize = 64 * 1024;

  Block alloc(size_t size, size_t align) noexcept;
  void free(void* p, size_t size, size_t align) noexcept;

  // Stays in place when the new size is in the same class.
  Block resize(void* p, size_t old_size, size_t want, size_t align) noexcept;

  bool owns(const void*) const noexcept { return true; }
};

// Small blocks on ThreadLocalPool, everything else on the system allocator.
using DefaultChain =
    Segregator<ThreadLocalPool::kMaxSmall,
               AlignSplit<kMaxAlign, ThreadLocalPool, SysBlock>, SysBlock>;

// The default chain as a Resource (immortal, thread-safe).
Resource* chain_resource() noexcept;
}  // namespace yasl::mem
