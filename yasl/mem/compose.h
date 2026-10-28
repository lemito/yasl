#pragma once

#include <yasl/core/check.h>

#include "resource.h"
#include "sys.h"

// Statically composed allocators.
//
// A building block is a class with a non-virtual interface:
//
//   Block alloc(size_t size, size_t align) noexcept;  // {} when it cannot
//   void  free(void* p, size_t size, size_t align) noexcept;
//   Block resize(void* p, size_t old, size_t want, size_t align) noexcept;
//   bool  owns(const void* p) const noexcept;
//
// Blocks combine with Fallback and Segregator; members are stored with
// [[no_unique_address]], so stateless blocks cost nothing. AsResource adds the
// one virtual layer that turns a composition into a mem::Resource.
//
//     AsResource<Fallback<StackBuffer<4096>, SysBlock>> r;
//     Vector<int> v(&r);        // stack first, heap when it overflows
namespace yasl::mem {
// Bump pointer over [begin, end). The most recent block can be rolled back or
// grown in place.
class BumpRegion {
 public:
  constexpr BumpRegion() noexcept = default;

  BumpRegion(void* begin, size_t size) noexcept
      : begin_(static_cast<char*>(begin)), cur_(begin_), end_(begin_ + size) {}

  Block alloc(size_t size, size_t align) noexcept {
    YASL_ASSERT(is_pow2(align));
    if (size == 0) {
      size = 1;
    }
    auto p = align_up(reinterpret_cast<uintptr_t>(cur_), align);
    auto e = reinterpret_cast<uintptr_t>(end_);
    if (!cur_ || p > e || e - p < size) {
      return {};
    }
    char* r = cur_ + (p - reinterpret_cast<uintptr_t>(cur_));
    cur_ = r + size;
    return {r, size};
  }

  // Rolls back `p` if it is the newest block; otherwise the memory is only
  // reclaimed by reset().
  void free(void* p, size_t size) noexcept {
    if (static_cast<char*>(p) + (size == 0 ? 1 : size) == cur_) {
      cur_ = static_cast<char*>(p);
    }
  }

  // Grows or shrinks the newest block in place. {} if `p` is not the newest or
  // there is no room.
  Block resize(void* p, size_t old_size, size_t want) noexcept {
    char* b = static_cast<char*>(p);
    if (b + (old_size == 0 ? 1 : old_size) != cur_ || want > size_t(end_ - b)) {
      return {};
    }
    cur_ = b + (want == 0 ? 1 : want);
    return {p, want};
  }

  bool owns(const void* p) const noexcept {
    auto c = static_cast<const char*>(p);
    return c >= begin_ && c < end_;
  }

  void reset() noexcept { cur_ = begin_; }

  size_t used() const noexcept { return size_t(cur_ - begin_); }

  size_t capacity() const noexcept { return size_t(end_ - begin_); }

  char* cur() const noexcept { return cur_; }

 private:
  char* begin_ = nullptr;
  char* cur_ = nullptr;
  char* end_ = nullptr;
};

// N bytes of inline storage with bump allocation. Not movable: the region
// points into the object.
template <size_t N>
class StackBuffer {
 public:
  StackBuffer() noexcept : region_(buffer_, N) {}

  StackBuffer(const StackBuffer&) = delete;
  StackBuffer& operator=(const StackBuffer&) = delete;

  Block alloc(size_t size, size_t align) noexcept {
    return region_.alloc(size, align);
  }

  void free(void* p, size_t size, size_t) noexcept { region_.free(p, size); }

  Block resize(void* p, size_t old_size, size_t want, size_t) noexcept {
    return region_.resize(p, old_size, want);
  }

  bool owns(const void* p) const noexcept { return region_.owns(p); }

  size_t used() const noexcept { return region_.used(); }

  void reset() noexcept { region_.reset(); }

 private:
  alignas(kMaxAlign) unsigned char buffer_[N];
  BumpRegion region_;
};

// The system allocator (mem::sys). Stateless.
struct SysBlock {
  Block alloc(size_t size, size_t align) noexcept {
    return sys::alloc(size, align);
  }

  void free(void* p, size_t size, size_t align) noexcept {
    sys::free(p, size, align);
  }

  Block resize(void* p, size_t old_size, size_t want, size_t align) noexcept {
    return sys::resize(p, old_size, want, align);
  }

  // It serves whatever nobody else claims.
  bool owns(const void*) const noexcept { return true; }
};

// Tries Primary first and Secondary when Primary cannot. Blocks go back to
// whoever owns them.
template <class Primary, class Secondary>
class Fallback {
 public:
  Block alloc(size_t size, size_t align) noexcept {
    Block b = primary_.alloc(size, align);
    return b.ptr ? b : secondary_.alloc(size, align);
  }

  void free(void* p, size_t size, size_t align) noexcept {
    if (primary_.owns(p)) {
      primary_.free(p, size, align);
    } else {
      secondary_.free(p, size, align);
    }
  }

  Block resize(void* p, size_t old_size, size_t want, size_t align) noexcept {
    return primary_.owns(p) ? primary_.resize(p, old_size, want, align)
                            : secondary_.resize(p, old_size, want, align);
  }

  bool owns(const void* p) const noexcept {
    return primary_.owns(p) || secondary_.owns(p);
  }

  Primary& primary() noexcept { return primary_; }

  Secondary& secondary() noexcept { return secondary_; }

 private:
  [[no_unique_address]] Primary primary_;
  [[no_unique_address]] Secondary secondary_;
};

// Routes by the requested size: <= Threshold goes to Small, the rest to Large.
//
// free() and resize() route by the size they are given, which is somewhere in
// [requested, reported]. That is only the same side of the threshold if Small
// never reports a block larger than Threshold, so alloc() clamps the reported
// size of Small blocks to Threshold.
template <size_t Threshold, class Small, class Large>
class Segregator {
 public:
  Block alloc(size_t size, size_t align) noexcept {
    if (size <= Threshold) {
      Block b = small_.alloc(size, align);
      if (b.size > Threshold) {
        b.size = Threshold;
      }
      return b;
    }
    return large_.alloc(size, align);
  }

  void free(void* p, size_t size, size_t align) noexcept {
    if (size <= Threshold) {
      small_.free(p, size, align);
    } else {
      large_.free(p, size, align);
    }
  }

  // Only within one side; crossing the threshold means moving the block.
  Block resize(void* p, size_t old_size, size_t want, size_t align) noexcept {
    if (old_size <= Threshold && want <= Threshold) {
      return small_.resize(p, old_size, want, align);
    }
    if (old_size > Threshold && want > Threshold) {
      return large_.resize(p, old_size, want, align);
    }
    return {};
  }

  bool owns(const void* p) const noexcept {
    return small_.owns(p) || large_.owns(p);
  }

 private:
  [[no_unique_address]] Small small_;
  [[no_unique_address]] Large large_;
};

// Routes by alignment: <= MaxAlign goes to Low, the rest to High.
template <size_t MaxAlign, class Low, class High>
class AlignSplit {
 public:
  Block alloc(size_t size, size_t align) noexcept {
    return align <= MaxAlign ? low_.alloc(size, align)
                             : high_.alloc(size, align);
  }

  void free(void* p, size_t size, size_t align) noexcept {
    if (align <= MaxAlign) {
      low_.free(p, size, align);
    } else {
      high_.free(p, size, align);
    }
  }

  Block resize(void* p, size_t old_size, size_t want, size_t align) noexcept {
    return align <= MaxAlign ? low_.resize(p, old_size, want, align)
                             : high_.resize(p, old_size, want, align);
  }

  bool owns(const void* p) const noexcept {
    return low_.owns(p) || high_.owns(p);
  }

 private:
  [[no_unique_address]] Low low_;
  [[no_unique_address]] High high_;
};

// The only virtual layer: a composition seen as a mem::Resource.
template <class Composite>
class AsResource final : public Resource {
 public:
  AsResource() = default;

  template <class... Args>
  explicit AsResource(Args&&... args) : c_(yasl::forward<Args>(args)...) {}

  Composite& composite() noexcept { return c_; }

 protected:
  Block do_alloc(size_t size, size_t align) override {
    Block b = c_.alloc(size, align);
    if (!b.ptr) {
      throw BadAlloc();
    }
    return b;
  }

  void do_free(void* p, size_t size, size_t align) noexcept override {
    c_.free(p, size, align);
  }

  // Composition blocks may move the bytes, so only for relocatable contents.
  Block do_resize(void* p, size_t old_size, size_t want, size_t align,
                  bool relocatable) override {
    return relocatable ? c_.resize(p, old_size, want, align) : Block{};
  }

 private:
  Composite c_;
};
}  // namespace yasl::mem
