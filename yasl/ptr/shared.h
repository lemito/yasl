#pragma once

#include <yasl/conc/ref_count.h>
#include <yasl/core/check.h>
#include <yasl/core/utility.h>
#include <yasl/mem/resource.h>

#include "unique.h"

namespace yasl {
template <class T>
class Shared;
template <class T>
class Weak;

namespace detail {
// Shared header of every Shared/Weak block. `weak` counts the Weak pointers
// plus one for all strong pointers together; the value dies with `strong`, the
// block with `weak`.
struct SharedBlock {
  RefCount strong{1};
  RefCount weak{1};
  mem::Resource* res;
  void (*drop_value)(SharedBlock*) noexcept;
  void (*free_block)(SharedBlock*) noexcept;

  SharedBlock(mem::Resource* r, void (*drop)(SharedBlock*) noexcept,
              void (*free)(SharedBlock*) noexcept)
      : res(r), drop_value(drop), free_block(free) {}

  void release_strong() noexcept {
    if (strong.release()) {
      drop_value(this);
      release_weak();
    }
  }

  void release_weak() noexcept {
    if (weak.release()) {
      free_block(this);
    }
  }
};

template <class U>
struct ValueBlock : SharedBlock {
  union {
    U value;
  };

  template <class... Args>
  explicit ValueBlock(mem::Resource* r, Args&&... args)
      : SharedBlock(
            r,
            [](SharedBlock* b) noexcept {
              yasl::destroy_at(
                  yasl::addressof(static_cast<ValueBlock*>(b)->value));
            },
            [](SharedBlock* b) noexcept {
              b->res->destroy(static_cast<ValueBlock*>(b));
            }) {
    yasl::construct_at(yasl::addressof(value), yasl::forward<Args>(args)...);
  }

  ~ValueBlock() {}
};
}  // namespace detail

// Atomically reference-counted shared pointer (shared_ptr / Rust Arc). One
// allocation holds counters and object, taken from a mem::Resource.
// Shared<Derived> converts to Shared<Base>.
//
//     auto cfg = make_shared<Config>(...);
//     auto copy = cfg;              // +1, no allocation
//     copy.make_mut().port = 8080;  // clones only if shared (copy-on-write)
//     Weak<Config> w = cfg;         // does not keep the object alive
template <class T>
class Shared {
 public:
  Shared() noexcept = default;

  Shared(decltype(nullptr)) noexcept {}

  Shared(const Shared& other) noexcept : p_(other.p_), b_(other.b_) {
    if (b_) {
      b_->strong.ref();
    }
  }

  Shared(Shared&& other) noexcept
      : p_(yasl::exchange(other.p_, nullptr)),
        b_(yasl::exchange(other.b_, nullptr)) {}

  template <class U>
    requires(!SameAs<U, T> && ConvertibleTo<U*, T*>)
  Shared(const Shared<U>& other) noexcept : p_(other.p_), b_(other.b_) {
    if (b_) {
      b_->strong.ref();
    }
  }

  template <class U>
    requires(!SameAs<U, T> && ConvertibleTo<U*, T*>)
  Shared(Shared<U>&& other) noexcept
      : p_(yasl::exchange(other.p_, nullptr)),
        b_(yasl::exchange(other.b_, nullptr)) {}

  ~Shared() { release(); }

  Shared& operator=(const Shared& other) noexcept {
    Shared(other).swap(*this);
    return *this;
  }

  Shared& operator=(Shared&& other) noexcept {
    Shared(yasl::move(other)).swap(*this);
    return *this;
  }

  template <class U, class... Args>
  static Shared make_in(mem::Resource* res, Args&&... args) {
    static_assert(ConvertibleTo<U*, T*>);
    auto* b =
        res->create<detail::ValueBlock<U>>(res, yasl::forward<Args>(args)...);
    return Shared(yasl::addressof(b->value), b);
  }

  // Takes over a Unique (a separate control block is allocated from the same
  // resource).
  static Shared from(Unique<T>&& u) {
    if (!u) {
      return Shared();
    }
    mem::Resource* res = u.resource();
    T* p = u.get();
    auto* b = res->create<detail::ValueBlock<Unique<T>>>(res, yasl::move(u));
    return Shared(p, b);
  }

  T* get() const noexcept { return p_; }

  T& operator*() const noexcept {
    YASL_ASSERT(p_);
    return *p_;
  }

  T* operator->() const noexcept {
    YASL_ASSERT(p_);
    return p_;
  }

  explicit operator bool() const noexcept { return p_ != nullptr; }

  size_t use_count() const noexcept { return b_ ? b_->strong.count() : 0; }

  size_t weak_count() const noexcept { return b_ ? b_->weak.count() - 1 : 0; }

  bool unique() const noexcept { return b_ && b_->strong.unique(); }

  // Mutable access with copy-on-write: clones the value if it is shared.
  // Weak pointers of a unique Shared see the change.
  T& make_mut()
    requires CopyConstructible<T>
  {
    YASL_ASSERT(b_);
    if (!b_->strong.unique()) {
      *this = make_in<T>(b_->res, static_cast<const T&>(*p_));
    }
    return *p_;
  }

  void reset() noexcept {
    release();
    p_ = nullptr;
    b_ = nullptr;
  }

  void swap(Shared& other) noexcept {
    yasl::swap(p_, other.p_);
    yasl::swap(b_, other.b_);
  }

  friend bool operator==(const Shared& a, const Shared& b) noexcept {
    return a.p_ == b.p_;
  }

  friend bool operator==(const Shared& a, decltype(nullptr)) noexcept {
    return a.p_ == nullptr;
  }

 private:
  template <class U>
  friend class Shared;
  template <class U>
  friend class Weak;

  Shared(T* p, detail::SharedBlock* b) noexcept : p_(p), b_(b) {}

  void release() noexcept {
    if (b_) {
      b_->release_strong();
    }
  }

  T* p_ = nullptr;
  detail::SharedBlock* b_ = nullptr;
};

// Non-owning handle to a Shared object; lock() yields a Shared while the object
// is alive.
template <class T>
class Weak {
 public:
  Weak() noexcept = default;

  Weak(const Shared<T>& s) noexcept : p_(s.p_), b_(s.b_) {
    if (b_) {
      b_->weak.ref();
    }
  }

  template <class U>
    requires(!SameAs<U, T> && ConvertibleTo<U*, T*>)
  Weak(const Shared<U>& s) noexcept : p_(s.p_), b_(s.b_) {
    if (b_) {
      b_->weak.ref();
    }
  }

  Weak(const Weak& other) noexcept : p_(other.p_), b_(other.b_) {
    if (b_) {
      b_->weak.ref();
    }
  }

  Weak(Weak&& other) noexcept
      : p_(yasl::exchange(other.p_, nullptr)),
        b_(yasl::exchange(other.b_, nullptr)) {}

  template <class U>
    requires(!SameAs<U, T> && ConvertibleTo<U*, T*>)
  Weak(const Weak<U>& other) noexcept : p_(other.p_), b_(other.b_) {
    if (b_) {
      b_->weak.ref();
    }
  }

  ~Weak() { release(); }

  Weak& operator=(const Weak& other) noexcept {
    Weak(other).swap(*this);
    return *this;
  }

  Weak& operator=(Weak&& other) noexcept {
    Weak(yasl::move(other)).swap(*this);
    return *this;
  }

  Weak& operator=(const Shared<T>& s) noexcept {
    Weak(s).swap(*this);
    return *this;
  }

  // Empty Shared if the object is gone.
  Shared<T> lock() const noexcept {
    if (b_ && b_->strong.try_ref()) {
      return Shared<T>(p_, b_);
    }
    return Shared<T>();
  }

  bool expired() const noexcept { return !b_ || b_->strong.count() == 0; }

  size_t use_count() const noexcept { return b_ ? b_->strong.count() : 0; }

  void reset() noexcept {
    release();
    p_ = nullptr;
    b_ = nullptr;
  }

  void swap(Weak& other) noexcept {
    yasl::swap(p_, other.p_);
    yasl::swap(b_, other.b_);
  }

 private:
  template <class U>
  friend class Weak;

  void release() noexcept {
    if (b_) {
      b_->release_weak();
    }
  }

  T* p_ = nullptr;
  detail::SharedBlock* b_ = nullptr;
};

template <class T, class... Args>
Shared<T> make_shared(Args&&... args) {
  return Shared<T>::template make_in<T>(mem::default_resource(),
                                        yasl::forward<Args>(args)...);
}

template <class T, class... Args>
Shared<T> make_shared_in(mem::Resource* res, Args&&... args) {
  return Shared<T>::template make_in<T>(res, yasl::forward<Args>(args)...);
}
}  // namespace yasl
