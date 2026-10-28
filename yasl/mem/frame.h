#pragma once

#include <yasl/core/check.h>

#include "compose.h"

namespace yasl::mem {
namespace detail {
// A Resource* seen as a composition block. Failure is {} instead of a throw.
class UpstreamBlock {
 public:
  void set(Resource* res) noexcept { res_ = res; }

  Block alloc(size_t size, size_t align) noexcept {
    try {
      return res_->allocate_at_least(size, align);
    } catch (...) {
      return {};
    }
  }

  void free(void* p, size_t size, size_t align) noexcept {
    res_->deallocate(p, size, align);
  }

  Block resize(void*, size_t, size_t, size_t) noexcept { return {}; }

  bool owns(const void*) const noexcept { return true; }

 private:
  Resource* res_ = nullptr;
};

// StackBuffer that counts the blocks still alive in it.
template <size_t N>
class CountedStack : public StackBuffer<N> {
 public:
  Block alloc(size_t size, size_t align) noexcept {
    Block b = StackBuffer<N>::alloc(size, align);
    live_ += b.ptr != nullptr;
    return b;
  }

  void free(void* p, size_t size, size_t align) noexcept {
    --live_;
    StackBuffer<N>::free(p, size, align);
  }

  size_t live() const noexcept { return live_; }

 private:
  size_t live_ = 0;
};
}  // namespace detail

// The innermost scope's memory lives on the stack: for the lifetime of a Frame
// every container created without an explicit resource first takes its memory
// from N bytes of inline storage and, when that is used up, from the resource
// that was the default before.
//
//     {
//       mem::Frame<4096> f;
//       Vector<int> v;  String s = ...;    // no heap traffic while it fits
//     }
//
// Nothing allocated in a Frame may outlive it. In Debug builds the destructor
// panics if blocks from the stack buffer are still alive (a container that was
// moved out of the scope, say).
template <size_t N>
class Frame {
 public:
  explicit Frame(Resource* upstream = default_resource()) noexcept
      : scope_(init(upstream)) {}

  ~Frame() { verify_no_live(); }

  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;

  Resource* resource() noexcept { return &res_; }

  // Bytes of the stack buffer in use.
  size_t used() noexcept { return stack().used(); }

  // Panics if something allocated here is still alive. Debug builds only.
  void verify_no_live() {
#ifndef NDEBUG
    YASL_INSIST(stack().live() == 0,
                "mem::Frame destroyed while memory allocated in it is alive");
#endif
  }

 private:
  using Chain = Fallback<detail::CountedStack<N>, detail::UpstreamBlock>;

  detail::CountedStack<N>& stack() noexcept {
    return res_.composite().primary();
  }

  Resource* init(Resource* upstream) noexcept {
    res_.composite().secondary().set(upstream);
    return &res_;
  }

  AsResource<Chain> res_;
  ScopedDefault scope_;
};
}  // namespace yasl::mem
