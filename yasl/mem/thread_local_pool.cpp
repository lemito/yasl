#include "thread_local_pool.h"

#include <atomic>

namespace yasl::mem {
namespace {
constexpr size_t kSlab = ThreadLocalPool::kSlabSize;
constexpr size_t kClasses = 28;
constexpr size_t kHeader = 128;

struct FreeNode {
  FreeNode* next;
};

struct Heap;

// Lives at the start of every slab. Fields other than `owner` and `remote` are
// touched only by the owning heap (under the shared heap's lock for the shared
// one).
struct Slab {
  std::atomic<Heap*> owner;
  std::atomic<FreeNode*> remote;  // blocks freed by other threads
  Slab* next = nullptr;           // list of slabs with room, per class
  Slab* prev = nullptr;
  Slab* all_next = nullptr;  // every slab of the heap
  Slab* all_prev = nullptr;
  FreeNode* free = nullptr;
  char* cur;
  char* end;
  uint32 live = 0;  // handed out and not yet seen coming back
  uint32 block;
  uint8 cls;
  bool listed = false;
};
static_assert(sizeof(Slab) <= kHeader);

size_t class_of(size_t size) noexcept {
  size_t need = size < 16 ? 16 : size;
  if (need <= 128) {
    return (need + 15) / 16 - 1;
  }
  size_t v = need - 1;
  size_t msb = size_t(bit_width(uint64(v))) - 1;
  return 8 + (msb - 7) * 4 + ((v >> (msb - 2)) & 3);
}

size_t block_of(size_t cls) noexcept {
  if (cls < 8) {
    return 16 * (cls + 1);
  }
  size_t o = (cls - 8) / 4;
  size_t j = (cls - 8) % 4;
  return (size_t(128) << o) + (j + 1) * (size_t(32) << o);
}

Slab* slab_of(const void* p) noexcept {
  return reinterpret_cast<Slab*>(reinterpret_cast<uintptr_t>(p) &
                                 ~uintptr_t(kSlab - 1));
}

struct Heap {
  Slab* avail[kClasses] = {};
  Slab* all = nullptr;

  void list(Slab* s) noexcept {
    s->prev = nullptr;
    s->next = avail[s->cls];
    if (s->next) {
      s->next->prev = s;
    }
    avail[s->cls] = s;
    s->listed = true;
  }

  void unlist(Slab* s) noexcept {
    (s->prev ? s->prev->next : avail[s->cls]) = s->next;
    if (s->next) {
      s->next->prev = s->prev;
    }
    s->next = s->prev = nullptr;
    s->listed = false;
  }

  void link_all(Slab* s) noexcept {
    s->all_prev = nullptr;
    s->all_next = all;
    if (all) {
      all->all_prev = s;
    }
    all = s;
  }

  void unlink_all(Slab* s) noexcept {
    (s->all_prev ? s->all_prev->all_next : all) = s->all_next;
    if (s->all_next) {
      s->all_next->all_prev = s->all_prev;
    }
  }

  // Takes the blocks other threads freed into the local free list.
  static bool drain(Slab* s) noexcept {
    FreeNode* n = s->remote.exchange(nullptr, std::memory_order_acquire);
    if (!n) {
      return false;
    }
    while (n) {
      FreeNode* next = n->next;
      n->next = s->free;
      s->free = n;
      --s->live;
      n = next;
    }
    return true;
  }

  Slab* new_slab(size_t cls) noexcept {
    Block b = sys::alloc(kSlab, kSlab);
    if (!b.ptr) {
      return nullptr;
    }
    Slab* s = ::new (b.ptr) Slab();
    s->owner.store(this, std::memory_order_relaxed);
    s->remote.store(nullptr, std::memory_order_relaxed);
    s->block = uint32(block_of(cls));
    s->cls = uint8(cls);
    s->cur = static_cast<char*>(b.ptr) + kHeader;
    s->end = s->cur + (kSlab - kHeader) / s->block * s->block;
    link_all(s);
    return s;
  }

  void release(Slab* s) noexcept {
    if (s->listed) {
      unlist(s);
    }
    unlink_all(s);
    sys::free(s, kSlab, kSlab);
  }

  void* alloc(size_t cls) noexcept {
    Slab* s = avail[cls];
    while (s) {
      if (s->free) {
        FreeNode* n = s->free;
        s->free = n->next;
        ++s->live;
        return n;
      }
      if (s->cur != s->end) {
        void* p = s->cur;
        s->cur += s->block;
        ++s->live;
        return p;
      }
      if (!drain(s)) {
        unlist(s);  // full; comes back when a block is freed
        s = avail[cls];
      }
    }
    s = new_slab(cls);
    if (!s) {
      return nullptr;
    }
    list(s);
    return alloc(cls);
  }

  // Free by the owner.
  void free(Slab* s, void* p) noexcept {
    auto* n = ::new (p) FreeNode{s->free};
    s->free = n;
    --s->live;
    if (!s->listed) {
      list(s);
    }
    // Keep one empty slab per class as a cache.
    if (s->live == 0 && (s->next || s->prev)) {
      release(s);
    }
  }

  // Takes a slab over from a dead thread's heap.
  void adopt(Slab* s) noexcept {
    s->owner.store(this, std::memory_order_release);
    link_all(s);
    drain(s);
    if (s->live == 0) {
      release(s);
    } else if (s->free || s->cur != s->end) {
      list(s);
    }
  }

  // Drops empty slabs after remote frees.
  void sweep() noexcept {
    for (Slab* s = all; s;) {
      Slab* next = s->all_next;
      drain(s);
      if (s->live == 0) {
        release(s);
      } else if (!s->listed && (s->free || s->cur != s->end)) {
        list(s);
      }
      s = next;
    }
  }
};

// The shared heap: late allocations from exiting threads and orphaned slabs.
// Guarded by a spin lock; constant-initialised so it is usable at any time.
struct Shared {
  std::atomic_flag lock = ATOMIC_FLAG_INIT;
  Heap heap;

  void acquire() noexcept {
    while (lock.test_and_set(std::memory_order_acquire)) {
    }
  }

  void unlock() noexcept { lock.clear(std::memory_order_release); }
};

constinit Shared g_shared;

thread_local Heap* t_heap = nullptr;
thread_local bool t_dead = false;

// Hands the thread's slabs to the shared heap when the thread ends.
struct Retire {
  ~Retire() {
    Heap* h = t_heap;
    t_heap = nullptr;
    t_dead = true;
    if (!h) {
      return;
    }
    g_shared.acquire();
    while (h->all) {
      Slab* s = h->all;
      h->unlink_all(s);
      if (s->listed) {
        h->unlist(s);
      }
      g_shared.heap.adopt(s);
    }
    g_shared.heap.sweep();
    g_shared.unlock();
    sys::free(h, sizeof(Heap), alignof(Heap));
  }
};

Heap* local_heap() noexcept {
  if (t_heap || t_dead) {
    return t_heap;
  }
  Block b = sys::alloc(sizeof(Heap), alignof(Heap));
  if (!b.ptr) {
    return nullptr;
  }
  t_heap = ::new (b.ptr) Heap();
  thread_local Retire retire;  // registers the exit hook
  (void)retire;
  return t_heap;
}
}  // namespace

Block ThreadLocalPool::alloc(size_t size, size_t align) noexcept {
  if (size > kMaxSmall || align > kMaxAlign) {
    return {};
  }
  size_t cls = class_of(size);
  void* p;
  if (Heap* h = local_heap()) {
    p = h->alloc(cls);
  } else {
    g_shared.acquire();
    p = g_shared.heap.alloc(cls);
    g_shared.unlock();
  }
  return p ? Block{p, block_of(cls)} : Block{};
}

void ThreadLocalPool::free(void* p, size_t, size_t) noexcept {
  Slab* s = slab_of(p);
  Heap* me = t_heap;
  if (me && s->owner.load(std::memory_order_acquire) == me) {
    me->free(s, p);
    return;
  }
  // Someone else's slab: leave the block for its owner.
  auto* n = static_cast<FreeNode*>(p);
  n->next = s->remote.load(std::memory_order_relaxed);
  while (!s->remote.compare_exchange_weak(n->next, n, std::memory_order_release,
                                          std::memory_order_relaxed)) {
  }
}

Block ThreadLocalPool::resize(void* p, size_t, size_t want,
                              size_t align) noexcept {
  if (want > kMaxSmall || align > kMaxAlign) {
    return {};
  }
  Slab* s = slab_of(p);
  if (class_of(want) != s->cls) {
    return {};
  }
  return {p, s->block};
}

namespace {
class ChainResource final : public Resource {
 protected:
  Block do_alloc(size_t size, size_t align) override {
    Block b = chain_.alloc(size, align);
    if (!b.ptr) {
      throw BadAlloc();
    }
    return b;
  }

  void do_free(void* p, size_t size, size_t align) noexcept override {
    chain_.free(p, size, align);
  }

 private:
  DefaultChain chain_;
};
}  // namespace

Resource* chain_resource() noexcept {
  alignas(ChainResource) static unsigned char storage[sizeof(ChainResource)];
  static ChainResource* instance = ::new (storage) ChainResource();
  return instance;
}
}  // namespace yasl::mem
