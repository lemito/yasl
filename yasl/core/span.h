#pragma once

#include "check.h"
#include "utility.h"

namespace yasl {
template <class T>
class Span;

// Consecutive pieces of at most n elements (the last one may be shorter); see
// Span::chunks.
template <class T>
class Chunks {
 public:
  class Iter {
   public:
    struct Sentinel {};

    Span<T> operator*() const noexcept;

    Iter& operator++() noexcept {
      size_t step = n_ < rest_ ? n_ : rest_;
      p_ += step;
      rest_ -= step;
      return *this;
    }

    friend bool operator==(const Iter& it, Sentinel) noexcept {
      return it.rest_ == 0;
    }

   private:
    friend class Chunks;

    Iter(T* p, size_t rest, size_t n) noexcept : p_(p), rest_(rest), n_(n) {}

    T* p_;
    size_t rest_;
    size_t n_;
  };

  Chunks(T* p, size_t size, size_t n) noexcept : p_(p), size_(size), n_(n) {}

  Iter begin() const noexcept { return Iter(p_, size_, n_); }

  typename Iter::Sentinel end() const noexcept { return {}; }

 private:
  T* p_;
  size_t size_;
  size_t n_;
};

// Every run of n neighbouring elements, one step apart (nothing if n > size);
// see Span::windows.
template <class T>
class Windows {
 public:
  class Iter {
   public:
    struct Sentinel {};

    Span<T> operator*() const noexcept;

    Iter& operator++() noexcept {
      ++p_;
      --count_;
      return *this;
    }

    friend bool operator==(const Iter& it, Sentinel) noexcept {
      return it.count_ == 0;
    }

   private:
    friend class Windows;

    Iter(T* p, size_t count, size_t n) noexcept : p_(p), count_(count), n_(n) {}

    T* p_;
    size_t count_;
    size_t n_;
  };

  Windows(T* p, size_t size, size_t n) noexcept
      : p_(p), count_(n <= size ? size - n + 1 : 0), n_(n) {}

  Iter begin() const noexcept { return Iter(p_, count_, n_); }

  typename Iter::Sentinel end() const noexcept { return {}; }

 private:
  T* p_;
  size_t count_;
  size_t n_;
};

// Non-owning view of a contiguous array.
template <class T>
class Span {
 public:
  using value_type = RemoveCV<T>;
  using iterator = T*;

  constexpr Span() noexcept = default;

  constexpr Span(T* data, size_t size) noexcept : p_(data), n_(size) {}

  template <size_t N>
  constexpr Span(T (&array)[N]) noexcept : p_(array), n_(N) {}

  // Any contiguous container: Vector, String, ...
  template <class C>
    requires(!SameAs<RemoveCV<C>, Span> &&
             requires(C& c) {
               { c.data() } -> ConvertibleTo<T*>;
               { c.size() } -> ConvertibleTo<size_t>;
             })
  constexpr Span(C& c) noexcept : p_(c.data()), n_(c.size()) {}

  template <class U>
    requires(!SameAs<U, T> && ConvertibleTo<U (*)[], T (*)[]>)
  constexpr Span(Span<U> other) noexcept : p_(other.data()), n_(other.size()) {}

  constexpr T* data() const noexcept { return p_; }

  constexpr size_t size() const noexcept { return n_; }

  constexpr bool empty() const noexcept { return n_ == 0; }

  constexpr T* begin() const noexcept { return p_; }

  constexpr T* end() const noexcept { return p_ + n_; }

  constexpr T& operator[](size_t i) const noexcept {
    YASL_ASSERT(i < n_);
    return p_[i];
  }

  constexpr Span first(size_t n) const noexcept {
    return {p_, yasl::min(n, n_)};
  }

  constexpr Span last(size_t n) const noexcept {
    n = yasl::min(n, n_);
    return {p_ + n_ - n, n};
  }

  constexpr Span subspan(size_t pos, size_t n = size_t(-1)) const noexcept {
    pos = yasl::min(pos, n_);
    return {p_ + pos, yasl::min(n, n_ - pos)};
  }

  // for (Span<T> piece : s.chunks(4)): pieces of 4 elements, the last may be
  // shorter. n > 0.
  constexpr Chunks<T> chunks(size_t n) const noexcept {
    YASL_ASSERT(n > 0);
    return {p_, n_, n};
  }

  // for (Span<T> w : s.windows(3)): {0,1,2}, {1,2,3}, ... n > 0.
  constexpr Windows<T> windows(size_t n) const noexcept {
    YASL_ASSERT(n > 0);
    return {p_, n_, n};
  }

 private:
  T* p_ = nullptr;
  size_t n_ = 0;
};

template <class T>
Span<T> Chunks<T>::Iter::operator*() const noexcept {
  return Span<T>(p_, n_ < rest_ ? n_ : rest_);
}

template <class T>
Span<T> Windows<T>::Iter::operator*() const noexcept {
  return Span<T>(p_, n_);
}
}  // namespace yasl
