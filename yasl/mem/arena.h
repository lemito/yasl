#pragma once

#include "compose.h"
#include "resource.h"

namespace yasl::mem {
// Monotonic bump allocator. Deallocation is (almost) free: memory comes back
// all at once in release()/reset() or in the destructor. The most recent
// allocation can be rolled back, which helps growing buffers.
//
// Not thread-safe; wrap in Synchronized<Arena> if needed.
class Arena : public Resource {
 public:
  explicit Arena(Resource* upstream = default_resource(),
                 size_t first_chunk = 4096) noexcept;

  // Uses `buffer` first, then falls back to chunks from `upstream`.
  Arena(void* buffer, size_t size,
        Resource* upstream = default_resource()) noexcept;

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  ~Arena() override { release(); }

  // Returns every chunk to upstream.
  void release() noexcept;

  // Rewinds to empty but keeps the newest (largest) chunk for reuse.
  void reset() noexcept;

  // Bytes handed out since the last release()/reset().
  size_t used() const noexcept { return used_; }

  // Bytes currently held from upstream.
  size_t reserved() const noexcept { return reserved_; }

  Resource* upstream() const noexcept { return upstream_; }

 protected:
  Block do_alloc(size_t size, size_t align) override;
  void do_free(void* ptr, size_t size, size_t align) noexcept override;

 private:
  struct Chunk {
    Chunk* prev;
    size_t size;
  };

  void grow(size_t size, size_t align);

  Resource* upstream_;
  char* initial_ = nullptr;
  size_t initial_size_ = 0;
  size_t next_chunk_;
  Chunk* chunks_ = nullptr;
  BumpRegion region_;
  size_t used_ = 0;
  size_t reserved_ = 0;
};

namespace detail {
template <size_t N>
struct InlineBuffer {
  alignas(max_align_t) unsigned char buffer_[N];
};
}  // namespace detail

// Arena with N bytes of inline storage: no heap traffic until it overflows.
//
//     InlineArena<1024> arena;
//     Vector<int> v(&arena);
template <size_t N>
class InlineArena : private detail::InlineBuffer<N>, public Arena {
 public:
  explicit InlineArena(Resource* upstream = default_resource()) noexcept
      : Arena(this->buffer_, N, upstream) {}
};
}  // namespace yasl::mem
