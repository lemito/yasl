#pragma once

#include <yasl/core/exception.h>
#include <yasl/core/types.h>
#include <yasl/core/utility.h>

namespace yasl::mem {
inline constexpr size_t kMaxAlign = alignof(max_align_t);

constexpr bool is_pow2(size_t x) noexcept {
  return x != 0 && (x & (x - 1)) == 0;
}

constexpr size_t align_up(size_t x, size_t align) noexcept {
  return (x + align - 1) & ~(align - 1);
}

// Result of an allocation: the pointer and the real usable size (>= requested).
struct Block {
  void* ptr = nullptr;
  size_t size = 0;
};

// Base class of every yasl memory resource.
//
//   * allocate_at_least() reports the real block size, containers use the
//   slack;
//   * create<T>() / destroy() are typed helpers;
//   * resources compose: most take an `upstream` they get their memory from.
//
// Deallocation contract: deallocate(p, n, align) accepts any n between the size
// that was requested and the size that allocate_at_least() reported.
//
// To use a Resource with std containers see yasl/compat/allocator.h and
// yasl/compat/pmr.h.
class Resource {
 public:
  virtual ~Resource() = default;

  void* allocate(size_t size, size_t align = kMaxAlign) {
    return do_alloc(size, align).ptr;
  }

  Block allocate_at_least(size_t size, size_t align = kMaxAlign) {
    return do_alloc(size, align);
  }

  void deallocate(void* ptr, size_t size, size_t align = kMaxAlign) noexcept {
    do_free(ptr, size, align);
  }

  bool is_equal(const Resource& other) const noexcept {
    return this == &other || do_is_equal(other);
  }

  template <class T, class... Args>
  T* create(Args&&... args) {
    void* p = allocate(sizeof(T), alignof(T));
    try {
      return ::new (p) T(yasl::forward<Args>(args)...);
    } catch (...) {
      deallocate(p, sizeof(T), alignof(T));
      throw;
    }
  }

  // Destroys an object made by create<T>(). T must be the exact dynamic type.
  template <class T>
  void destroy(T* p) noexcept {
    if (p) {
      p->~T();
      deallocate(p, sizeof(T), alignof(T));
    }
  }

  template <class T>
  T* allocate_array(size_t n) {
    if (n > max_value<size_t>() / sizeof(T)) {
      throw BadAlloc();
    }
    return static_cast<T*>(allocate(n * sizeof(T), alignof(T)));
  }

  template <class T>
  void deallocate_array(T* p, size_t n) noexcept {
    deallocate(p, n * sizeof(T), alignof(T));
  }

 protected:
  virtual Block do_alloc(size_t size, size_t align) = 0;
  virtual void do_free(void* ptr, size_t size, size_t align) noexcept = 0;

  virtual bool do_is_equal(const Resource&) const noexcept { return false; }
};

// malloc-backed, thread-safe, never destroyed.
Resource* system_resource() noexcept;

// Throws BadAlloc on every allocation. Handy as an arena upstream to forbid
// growth.
Resource* null_resource() noexcept;

// Per-thread default resource used by every yasl container constructed without
// one.
Resource* default_resource() noexcept;

// Sets the default resource for the current thread (nullptr = system). Returns
// the previous one.
Resource* set_default_resource(Resource* res) noexcept;

// RAII: makes `res` the default resource of the current thread for the scope
// lifetime.
class ScopedDefault {
 public:
  explicit ScopedDefault(Resource* res) noexcept
      : prev_(set_default_resource(res)) {}

  ~ScopedDefault() { set_default_resource(prev_); }

  ScopedDefault(const ScopedDefault&) = delete;
  ScopedDefault& operator=(const ScopedDefault&) = delete;

 private:
  Resource* prev_;
};
}  // namespace yasl::mem
