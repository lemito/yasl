#pragma once

#include <yasl/conc/ref_count.h>
#include <yasl/core/check.h>
#include <yasl/core/traits.h>
#include <yasl/core/utility.h>
#include <yasl/mem/resource.h>

namespace yasl {
template <class T>
class IntrusivePtr;

enum class Counting {
  Atomic,
  Plain,
};

namespace detail {
template <Counting C>
class Counter;

template <>
class Counter<Counting::Atomic> {
 public:
  void ref() noexcept { n_.ref(); }
  bool unref() noexcept { return n_.release(); }
  size_t count() const noexcept { return n_.count(); }

 private:
  RefCount n_{0};
};

template <>
class Counter<Counting::Plain> {
 public:
  void ref() noexcept { ++n_; }
  bool unref() noexcept { return --n_ == 0; }
  size_t count() const noexcept { return n_; }

 private:
  size_t n_ = 0;
};
}  // namespace detail

// Base class that keeps the reference count inside the object (CRTP):
//
//     struct Node: RefCounted<Node> { int value; };
//     IntrusivePtr<Node> n = make_intrusive<Node>();
//     IntrusivePtr<Node> same = n->from_this();     // from inside a method,
//     too
//
// `C` picks the counter: Atomic (default) is safe across threads, Plain is
// cheaper but for single-threaded use. An object that was not made by
// make_intrusive*() (a global, a stack object) is never destroyed by the
// pointers, whoever holds them.
template <class T, Counting C = Counting::Atomic>
class RefCounted {
 public:
  using IntrusiveBase = RefCounted;

  RefCounted(const RefCounted&) noexcept {
  }  // copies start with their own count
  RefCounted& operator=(const RefCounted&) noexcept { return *this; }

  size_t use_count() const noexcept { return count_.count(); }

  // A new strong reference to *this.
  IntrusivePtr<T> from_this() noexcept;
  IntrusivePtr<const T> from_this() const noexcept;

 protected:
  RefCounted() noexcept = default;
  ~RefCounted() = default;

 private:
  template <class U>
  friend class IntrusivePtr;

  template <class U, class... Args>
  friend IntrusivePtr<U> make_intrusive_in(mem::Resource*, Args&&...);

  void ref() const noexcept { count_.ref(); }

  void unref() const noexcept {
    if (count_.unref() && drop_) {
      drop_(res_, this);
    }
  }

  mutable detail::Counter<C> count_;
  mem::Resource* res_ = nullptr;
  void (*drop_)(mem::Resource*, const RefCounted*) noexcept = nullptr;
};

// Shared owner of a RefCounted object; exactly one pointer wide.
template <class T>
class IntrusivePtr {
 public:
  IntrusivePtr() noexcept = default;
  IntrusivePtr(decltype(nullptr)) noexcept {}

  IntrusivePtr(const IntrusivePtr& other) noexcept : p_(other.p_) {
    if (p_) {
      p_->ref();
    }
  }

  IntrusivePtr(IntrusivePtr&& other) noexcept
      : p_(yasl::exchange(other.p_, nullptr)) {}

  template <class U>
    requires(!SameAs<U, T> && ConvertibleTo<U*, T*>)
  IntrusivePtr(const IntrusivePtr<U>& other) noexcept : p_(other.p_) {
    if (p_) {
      p_->ref();
    }
  }

  template <class U>
    requires(!SameAs<U, T> && ConvertibleTo<U*, T*>)
  IntrusivePtr(IntrusivePtr<U>&& other) noexcept
      : p_(yasl::exchange(other.p_, nullptr)) {}

  ~IntrusivePtr() { drop(); }

  IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
    IntrusivePtr(other).swap(*this);
    return *this;
  }

  IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
    IntrusivePtr(yasl::move(other)).swap(*this);
    return *this;
  }

  IntrusivePtr& operator=(decltype(nullptr)) noexcept {
    reset();
    return *this;
  }

  // Takes over a reference the caller already owns (no increment).
  static IntrusivePtr adopt(T* p) noexcept {
    IntrusivePtr out;
    out.p_ = p;
    return out;
  }

  // Adds a new reference to an object that is already managed.
  static IntrusivePtr retain(T* p) noexcept {
    if (p) {
      p->ref();
    }
    return adopt(p);
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

  // Gives the reference to the caller (no decrement).
  T* release() noexcept { return yasl::exchange(p_, nullptr); }

  void reset() noexcept {
    drop();
    p_ = nullptr;
  }

  void swap(IntrusivePtr& other) noexcept { yasl::swap(p_, other.p_); }

  friend void swap(IntrusivePtr& a, IntrusivePtr& b) noexcept { a.swap(b); }

  friend bool operator==(const IntrusivePtr& a,
                         const IntrusivePtr& b) noexcept {
    return a.p_ == b.p_;
  }

  friend bool operator==(const IntrusivePtr& a, decltype(nullptr)) noexcept {
    return a.p_ == nullptr;
  }

 private:
  template <class U>
  friend class IntrusivePtr;

  void drop() noexcept {
    if (p_) {
      p_->unref();
    }
  }

  T* p_ = nullptr;
};

template <class T, Counting C>
IntrusivePtr<T> RefCounted<T, C>::from_this() noexcept {
  return IntrusivePtr<T>::retain(static_cast<T*>(this));
}

template <class T, Counting C>
IntrusivePtr<const T> RefCounted<T, C>::from_this() const noexcept {
  return IntrusivePtr<const T>::retain(static_cast<const T*>(this));
}

// Constructs a U in `res`; the last pointer to it destroys it there.
template <class U, class... Args>
IntrusivePtr<U> make_intrusive_in(mem::Resource* res, Args&&... args) {
  using Base = typename U::IntrusiveBase;
  U* p = res->create<U>(yasl::forward<Args>(args)...);
  Base* base = p;
  base->res_ = res;
  base->drop_ = [](mem::Resource* r, const Base* b) noexcept {
    r->destroy(static_cast<U*>(const_cast<Base*>(b)));
  };
  return IntrusivePtr<U>::retain(p);
}

template <class U, class... Args>
IntrusivePtr<U> make_intrusive(Args&&... args) {
  return make_intrusive_in<U>(mem::default_resource(),
                              yasl::forward<Args>(args)...);
}
}  // namespace yasl
