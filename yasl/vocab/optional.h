#pragma once

#include <yasl/core/check.h>
#include <yasl/core/exception.h>
#include <yasl/core/utility.h>

namespace yasl {
template <class T, class E>
class Result;
template <class T>
struct Ok;
template <class E>
struct Err;

struct None {
  explicit constexpr None(int) noexcept {}
};

inline constexpr None none{0};

// A value or nothing.
template <class T>
class Optional {
 public:
  using value_type = T;

  Optional() noexcept {}

  Optional(None) noexcept {}

  template <class U = T>
    requires(ConstructibleFrom<T, U &&> && !SameAs<RemoveCVRef<U>, Optional> &&
             !SameAs<RemoveCVRef<U>, None>)
  Optional(U&& value) {
    yasl::construct_at(yasl::addressof(value_), yasl::forward<U>(value));
    has_ = true;
  }

  Optional(const Optional& other) {
    if (other.has_) {
      yasl::construct_at(yasl::addressof(value_), other.value_);
      has_ = true;
    }
  }

  Optional(Optional&& other) noexcept(NothrowMoveConstructible<T>) {
    if (other.has_) {
      yasl::construct_at(yasl::addressof(value_), yasl::move(other.value_));
      has_ = true;
    }
  }

  ~Optional() { reset(); }

  Optional& operator=(None) noexcept {
    reset();
    return *this;
  }

  Optional& operator=(const Optional& other) {
    if (this != &other) {
      if (other.has_) {
        assign(other.value_);
      } else {
        reset();
      }
    }
    return *this;
  }

  Optional& operator=(Optional&& other) noexcept(NothrowMoveConstructible<T>) {
    if (this != &other) {
      if (other.has_) {
        assign(yasl::move(other.value_));
      } else {
        reset();
      }
    }
    return *this;
  }

  template <class U = T>
    requires(ConstructibleFrom<T, U &&> && !SameAs<RemoveCVRef<U>, Optional> &&
             !SameAs<RemoveCVRef<U>, None>)
  Optional& operator=(U&& value) {
    assign(yasl::forward<U>(value));
    return *this;
  }

  template <class... Args>
  T& emplace(Args&&... args) {
    reset();
    yasl::construct_at(yasl::addressof(value_), yasl::forward<Args>(args)...);
    has_ = true;
    return value_;
  }

  void reset() noexcept {
    if (has_) {
      yasl::destroy_at(yasl::addressof(value_));
      has_ = false;
    }
  }

  bool has_value() const noexcept { return has_; }

  explicit operator bool() const noexcept { return has_; }

  T& operator*() & noexcept {
    YASL_ASSERT(has_);
    return value_;
  }

  const T& operator*() const& noexcept {
    YASL_ASSERT(has_);
    return value_;
  }

  T&& operator*() && noexcept {
    YASL_ASSERT(has_);
    return yasl::move(value_);
  }

  T* operator->() noexcept {
    YASL_ASSERT(has_);
    return yasl::addressof(value_);
  }

  const T* operator->() const noexcept {
    YASL_ASSERT(has_);
    return yasl::addressof(value_);
  }

  T& value() & {
    check();
    return value_;
  }

  const T& value() const& {
    check();
    return value_;
  }

  T&& value() && {
    check();
    return yasl::move(value_);
  }

  template <class U>
  T value_or(U&& fallback) const& {
    return has_ ? value_ : T(yasl::forward<U>(fallback));
  }

  template <class F>
  T value_or_else(F&& f) const& {
    return has_ ? value_ : T(f());
  }

  template <class F>
  T value_or_else(F&& f) && {
    return has_ ? yasl::move(value_) : T(f());
  }

  // f(T) -> U gives Optional<U>; empty stays empty.
  template <class F>
  auto map(
      F&& f) const& -> Optional<RemoveCVRef<decltype(f(declval<const T&>()))>> {
    if (has_) {
      return f(value_);
    }
    return none;
  }

  template <class F>
  auto map(F&& f) && -> Optional<RemoveCVRef<decltype(f(declval<T>()))>> {
    if (has_) {
      return f(yasl::move(value_));
    }
    return none;
  }

  // f(T) -> Optional<U>.
  template <class F>
  auto and_then(F&& f) const& -> RemoveCVRef<decltype(f(declval<const T&>()))> {
    if (has_) {
      return f(value_);
    }
    return none;
  }

  template <class F>
  auto and_then(F&& f) && -> RemoveCVRef<decltype(f(declval<T>()))> {
    if (has_) {
      return f(yasl::move(value_));
    }
    return none;
  }

  // f() -> Optional<T>, called only when empty.
  template <class F>
  Optional or_else(F&& f) const& {
    return has_ ? *this : Optional(f());
  }

  template <class F>
  Optional or_else(F&& f) && {
    return has_ ? Optional(yasl::move(*this)) : Optional(f());
  }

  // Keeps the value only if pred(value) holds.
  template <class P>
  Optional filter(P&& pred) && {
    if (has_ && pred(value_)) {
      return Optional(yasl::move(*this));
    }
    return none;
  }

  // Takes the value out, leaving this empty.
  Optional take() noexcept(NothrowMoveConstructible<T>) {
    Optional out(yasl::move(*this));
    reset();
    return out;
  }

  // Value or the given error (needs yasl/vocab/result.h).
  template <class E>
  Result<T, RemoveCVRef<E>> ok_or(E&& e) && {
    if (has_) {
      return Ok<T>{yasl::move(value_)};
    }
    return Err<RemoveCVRef<E>>{yasl::forward<E>(e)};
  }

  // nullptr if empty.
  T* get() noexcept { return has_ ? yasl::addressof(value_) : nullptr; }

  const T* get() const noexcept {
    return has_ ? yasl::addressof(value_) : nullptr;
  }

  friend bool operator==(const Optional& a, const Optional& b)
    requires EqualityComparable<T>
  {
    return a.has_ == b.has_ && (!a.has_ || a.value_ == b.value_);
  }

  friend bool operator==(const Optional& a, None) noexcept { return !a.has_; }

  template <class U>
    requires(!SameAs<U, Optional> && !SameAs<U, None> &&
             EqualityComparableWith<T, U>)
  friend bool operator==(const Optional& a, const U& b) {
    return a.has_ && a.value_ == b;
  }

 private:
  template <class U>
  void assign(U&& v) {
    if (has_) {
      value_ = yasl::forward<U>(v);
    } else {
      yasl::construct_at(yasl::addressof(value_), yasl::forward<U>(v));
      has_ = true;
    }
  }

  void check() const {
    if (!has_) {
      throw BadAccess("yasl::Optional: no value");
    }
  }

  union {
    T value_;
  };
  bool has_ = false;
};

// Optional reference: a nullable pointer with Optional's interface.
template <class T>
class Optional<T&> {
 public:
  using value_type = T;

  constexpr Optional() noexcept = default;

  constexpr Optional(None) noexcept {}

  constexpr Optional(T& ref) noexcept : p_(yasl::addressof(ref)) {}

  Optional(T&&) = delete;

  constexpr bool has_value() const noexcept { return p_ != nullptr; }

  constexpr explicit operator bool() const noexcept { return p_ != nullptr; }

  constexpr T& operator*() const noexcept {
    YASL_ASSERT(p_);
    return *p_;
  }

  constexpr T* operator->() const noexcept {
    YASL_ASSERT(p_);
    return p_;
  }

  T& value() const {
    if (!p_) {
      throw BadAccess("yasl::Optional: no value");
    }
    return *p_;
  }

  constexpr T* get() const noexcept { return p_; }

  template <class U>
  T value_or(U&& fallback) const {
    return p_ ? *p_ : T(yasl::forward<U>(fallback));
  }

  constexpr void reset() noexcept { p_ = nullptr; }

  constexpr Optional take() noexcept {
    return Optional(yasl::exchange(p_, nullptr));
  }

  template <class F>
  auto map(F&& f) const -> Optional<RemoveCVRef<decltype(f(declval<T&>()))>> {
    if (p_) {
      return f(*p_);
    }
    return none;
  }

  template <class F>
  auto and_then(F&& f) const -> RemoveCVRef<decltype(f(declval<T&>()))> {
    if (p_) {
      return f(*p_);
    }
    return none;
  }

  template <class P>
  Optional filter(P&& pred) const {
    return (p_ && pred(*p_)) ? *this : Optional();
  }

  template <class F>
  Optional or_else(F&& f) const {
    return p_ ? *this : Optional(f());
  }

  friend constexpr bool operator==(const Optional& a, None) noexcept {
    return !a.p_;
  }

 private:
  explicit constexpr Optional(T* p) noexcept : p_(p) {}

  T* p_ = nullptr;
};
}  // namespace yasl

#include "result.h"
