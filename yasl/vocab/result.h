#pragma once

#include <yasl/core/check.h>
#include <yasl/core/exception.h>
#include <yasl/core/utility.h>

#include "optional.h"

namespace yasl {
// Tags produced by ok() / err(); convert implicitly to any matching Result.
template <class T>
struct Ok {
  T value;
};

template <>
struct Ok<void> {};

template <class E>
struct Err {
  E error;
};

template <class T>
constexpr Ok<RemoveCVRef<T>> ok(T&& v) {
  return Ok<RemoveCVRef<T>>{yasl::forward<T>(v)};
}

constexpr Ok<void> ok() noexcept { return {}; }

template <class E>
constexpr Err<RemoveCVRef<E>> err(E&& e) {
  return Err<RemoveCVRef<E>>{yasl::forward<E>(e)};
}

// Value or error, for expected failures (exceptions stay for contract
// violations).
//
//     Result<int, io::Error> r = parse();
//     if (r) use(*r); else log(r.error());
template <class T, class E>
class Result {
 public:
  using value_type = T;
  using error_type = E;

  template <class U>
    requires ConstructibleFrom<T, U&&>
  Result(Ok<U> o) {
    yasl::construct_at(yasl::addressof(value_), yasl::move(o.value));
    ok_ = true;
  }

  template <class U>
    requires ConstructibleFrom<E, U&&>
  Result(Err<U> e) {
    yasl::construct_at(yasl::addressof(error_), yasl::move(e.error));
  }

  Result(const Result& other) { copy_from(other); }

  Result(Result&& other) noexcept(NothrowMoveConstructible<T> &&
                                  NothrowMoveConstructible<E>) {
    move_from(yasl::move(other));
  }

  Result& operator=(const Result& other) {
    if (this != &other) {
      destroy();
      copy_from(other);
    }
    return *this;
  }

  Result& operator=(Result&& other) noexcept(NothrowMoveConstructible<T> &&
                                             NothrowMoveConstructible<E>) {
    if (this != &other) {
      destroy();
      move_from(yasl::move(other));
    }
    return *this;
  }

  ~Result() { destroy(); }

  bool is_ok() const noexcept { return ok_; }

  bool is_err() const noexcept { return !ok_; }

  explicit operator bool() const noexcept { return ok_; }

  T& operator*() & noexcept {
    YASL_ASSERT(ok_);
    return value_;
  }

  const T& operator*() const& noexcept {
    YASL_ASSERT(ok_);
    return value_;
  }

  T&& operator*() && noexcept {
    YASL_ASSERT(ok_);
    return yasl::move(value_);
  }

  T* operator->() noexcept {
    YASL_ASSERT(ok_);
    return yasl::addressof(value_);
  }

  const T* operator->() const noexcept {
    YASL_ASSERT(ok_);
    return yasl::addressof(value_);
  }

  T& value() & {
    check_ok();
    return value_;
  }

  const T& value() const& {
    check_ok();
    return value_;
  }

  T&& value() && {
    check_ok();
    return yasl::move(value_);
  }

  E& error() & noexcept {
    YASL_ASSERT(!ok_);
    return error_;
  }

  const E& error() const& noexcept {
    YASL_ASSERT(!ok_);
    return error_;
  }

  E&& error() && noexcept {
    YASL_ASSERT(!ok_);
    return yasl::move(error_);
  }

  template <class U>
  T value_or(U&& fallback) const& {
    return ok_ ? value_ : T(yasl::forward<U>(fallback));
  }

  template <class U>
  T value_or(U&& fallback) && {
    return ok_ ? yasl::move(value_) : T(yasl::forward<U>(fallback));
  }

  // f(T) -> U; the error passes through.
  template <class F>
  auto map(F&& f) && -> Result<decltype(f(declval<T>())), E> {
    using U = decltype(f(declval<T>()));
    if (ok_) {
      return Ok<U>{f(yasl::move(value_))};
    }
    return Err<E>{yasl::move(error_)};
  }

  // f(E) -> E2; the value passes through.
  template <class F>
  auto map_err(F&& f) && -> Result<T, decltype(f(declval<E>()))> {
    using E2 = decltype(f(declval<E>()));
    if (ok_) {
      return Ok<T>{yasl::move(value_)};
    }
    return Err<E2>{f(yasl::move(error_))};
  }

  // f(T) -> Result<U, E>.
  template <class F>
  auto and_then(F&& f) && -> decltype(f(declval<T>())) {
    if (ok_) {
      return f(yasl::move(value_));
    }
    return Err<E>{yasl::move(error_)};
  }

  // f(E) -> Result<T, E2>.
  template <class F>
  auto or_else(F&& f) && -> decltype(f(declval<E>())) {
    if (ok_) {
      return Ok<T>{yasl::move(value_)};
    }
    return f(yasl::move(error_));
  }

  Optional<T> ok() && {
    if (ok_) {
      return Optional<T>(yasl::move(value_));
    }
    return none;
  }

 private:
  void destroy() noexcept {
    if (ok_) {
      yasl::destroy_at(yasl::addressof(value_));
    } else {
      yasl::destroy_at(yasl::addressof(error_));
    }
  }

  void copy_from(const Result& other) {
    if (other.ok_) {
      yasl::construct_at(yasl::addressof(value_), other.value_);
    } else {
      yasl::construct_at(yasl::addressof(error_), other.error_);
    }
    ok_ = other.ok_;
  }

  void move_from(Result&& other) {
    if (other.ok_) {
      yasl::construct_at(yasl::addressof(value_), yasl::move(other.value_));
    } else {
      yasl::construct_at(yasl::addressof(error_), yasl::move(other.error_));
    }
    ok_ = other.ok_;
  }

  void check_ok() const {
    if (!ok_) {
      throw BadAccess("yasl::Result: holds an error");
    }
  }

  union {
    T value_;
    E error_;
  };
  bool ok_ = false;
};

template <class E>
class Result<void, E> {
 public:
  using value_type = void;
  using error_type = E;

  Result(Ok<void>) noexcept {}

  template <class U>
    requires ConstructibleFrom<E, U&&>
  Result(Err<U> e) : error_(yasl::move(e.error)) {}

  bool is_ok() const noexcept { return !error_.has_value(); }

  bool is_err() const noexcept { return error_.has_value(); }

  explicit operator bool() const noexcept { return !error_.has_value(); }

  void value() const {
    if (error_) {
      throw BadAccess("yasl::Result: holds an error");
    }
  }

  E& error() & noexcept {
    YASL_ASSERT(error_);
    return *error_;
  }

  const E& error() const& noexcept {
    YASL_ASSERT(error_);
    return *error_;
  }

  E&& error() && noexcept {
    YASL_ASSERT(error_);
    return yasl::move(*error_);
  }

  template <class F>
  auto map_err(F&& f) && -> Result<void, decltype(f(declval<E>()))> {
    using E2 = decltype(f(declval<E>()));
    if (!error_) {
      return Ok<void>{};
    }
    return Err<E2>{f(yasl::move(*error_))};
  }

  // f() -> Result<U, E>.
  template <class F>
  auto and_then(F&& f) && -> decltype(f()) {
    if (!error_) {
      return f();
    }
    return Err<E>{yasl::move(*error_)};
  }

 private:
  Optional<E> error_;
};
}  // namespace yasl

// Unwraps a Result or returns its error from the enclosing function (GCC/clang
// statement expression).
//
//     int n = YASL_TRY(parse(s));
#define YASL_TRY(expr)                                     \
  ({                                                       \
    auto yasl_try_ = (expr);                               \
    if (!yasl_try_) {                                      \
      return ::yasl::err(::yasl::move(yasl_try_).error()); \
    }                                                      \
    ::yasl::move(*yasl_try_);                              \
  })

#define YASL_TRY_VOID(expr)                                \
  do {                                                     \
    auto yasl_try_ = (expr);                               \
    if (!yasl_try_) {                                      \
      return ::yasl::err(::yasl::move(yasl_try_).error()); \
    }                                                      \
  } while (0)
