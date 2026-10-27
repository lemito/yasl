#pragma once

#include <yasl/conc/mutex.h>

#include "resource.h"

namespace yasl::mem {
// Makes any resource thread-safe by serializing it with a mutex.
//
//     Synchronized<Pool> shared_pool(upstream);
template <class R>
class Synchronized final : public Resource {
 public:
  template <class... Args>
  explicit Synchronized(Args&&... args)
      : inner_(yasl::forward<Args>(args)...) {}

  // Unsynchronized access to the wrapped resource.
  R& inner() noexcept { return inner_; }

  template <class F>
  decltype(auto) with_lock(F&& f) {
    LockGuard lock(mutex_);
    return yasl::forward<F>(f)(inner_);
  }

 protected:
  Block do_alloc(size_t size, size_t align) override {
    LockGuard lock(mutex_);
    return inner_.allocate_at_least(size, align);
  }

  void do_free(void* ptr, size_t size, size_t align) noexcept override {
    LockGuard lock(mutex_);
    inner_.deallocate(ptr, size, align);
  }

 private:
  Mutex mutex_;
  R inner_;
};
}  // namespace yasl::mem
