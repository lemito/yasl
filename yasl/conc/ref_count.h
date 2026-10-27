#pragma once

#include <atomic>
#include <cstddef>

namespace yasl {
// Thread-safe reference counter used by Arc and the copy-on-write String.
class RefCount {
 public:
  explicit RefCount(size_t initial = 1) noexcept : n_(initial) {}

  RefCount(const RefCount&) = delete;
  RefCount& operator=(const RefCount&) = delete;

  void ref() noexcept { n_.fetch_add(1, std::memory_order_relaxed); }

  // Returns true when the caller dropped the last reference and must destroy
  // the object.
  bool unref() noexcept {
    // Sole owner: nobody else can increment the counter, so skip the RMW.
    if (n_.load(std::memory_order_acquire) == 1) {
      return true;
    }
    return n_.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }

  // Like unref() but always a real RMW: needed when try_ref() can race with the
  // drop (weak pointers).
  bool release() noexcept {
    return n_.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }

  // Increments unless the count is already zero; returns whether it did.
  bool try_ref() noexcept {
    size_t n = n_.load(std::memory_order_relaxed);
    while (n != 0) {
      if (n_.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel,
                                   std::memory_order_relaxed)) {
        return true;
      }
    }
    return false;
  }

  bool unique() const noexcept {
    return n_.load(std::memory_order_acquire) == 1;
  }

  size_t count() const noexcept { return n_.load(std::memory_order_relaxed); }

 private:
  std::atomic<size_t> n_;
};
}  // namespace yasl
