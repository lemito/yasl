#pragma once

#include <yasl/core/noncopy.h>
#include <yasl/core/check.h>
#include <yasl/core/traits.h>

#include <atomic>

namespace yasl {
// Memory orders for Atomic<T>.
enum class Order : int {
  Relaxed = int(std::memory_order_relaxed),
  Acquire = int(std::memory_order_acquire),
  Release = int(std::memory_order_release),
  AcqRel = int(std::memory_order_acq_rel),
  SeqCst = int(std::memory_order_seq_cst),
};

// Thin wrapper over std::atomic with the memory order spelled out at every call
// (Rust AtomicXxx). The default is the safe SeqCst.
//
//     Atomic<int> n{0};
//     n.fetch_add(1, Order::Relaxed);
template <class T>
  requires(TriviallyCopyable<T>)
class Atomic : NoneCopyable {
 public:
  constexpr Atomic() noexcept = default;

  constexpr explicit Atomic(T v) noexcept : a_(v) {}

  Atomic(const Atomic&) = delete;
  Atomic& operator=(const Atomic&) = delete;

  T load(Order o = Order::SeqCst) const noexcept { return a_.load(order(o)); }

  void store(T v, Order o = Order::SeqCst) noexcept { a_.store(v, order(o)); }

  T exchange(T v, Order o = Order::SeqCst) noexcept {
    return a_.exchange(v, order(o));
  }

  // The one-order overload derives a valid failure order.
  bool compare_exchange(T& expected, T desired, Order success = Order::SeqCst) noexcept {
    return compare_exchange(expected, desired, success, failure_order(success));
  }

  bool compare_exchange(T& expected, T desired, Order success, Order failure) noexcept {
    YASL_VERIFY(valid_failure(success, failure));
    return a_.compare_exchange_strong(expected, desired, order(success), order(failure));
  }

  bool compare_exchange_weak(T& expected, T desired, Order success = Order::SeqCst) noexcept {
    return compare_exchange_weak(expected, desired, success, failure_order(success));
  }

  bool compare_exchange_weak(T& expected, T desired, Order success, Order failure) noexcept {
    YASL_VERIFY(valid_failure(success, failure));
    return a_.compare_exchange_weak(expected, desired, order(success), order(failure));
  }

  T fetch_add(Conditional<Pointer<T>, ptrdiff_t, T> v, Order o = Order::SeqCst) noexcept
    requires((Integral<T> && !SameAs<T, bool>) || Pointer<T>)
  {
    return a_.fetch_add(v, order(o));
  }

  T fetch_sub(Conditional<Pointer<T>, ptrdiff_t, T> v, Order o = Order::SeqCst) noexcept
    requires((Integral<T> && !SameAs<T, bool>) || Pointer<T>)
  {
    return a_.fetch_sub(v, order(o));
  }

  T fetch_and(T v, Order o = Order::SeqCst) noexcept
    requires(Integral<T> && !SameAs<T, bool>)
  {
    return a_.fetch_and(v, order(o));
  }

  T fetch_or(T v, Order o = Order::SeqCst) noexcept
    requires(Integral<T> && !SameAs<T, bool>)
  {
    return a_.fetch_or(v, order(o));
  }

  T fetch_xor(T v, Order o = Order::SeqCst) noexcept
    requires(Integral<T> && !SameAs<T, bool>)
  {
    return a_.fetch_xor(v, order(o));
  }

  // Retries f(current) -> new until the swap succeeds; returns the previous
  // value.
  template <class F>
  T update(F f, Order o = Order::SeqCst) noexcept(noexcept(f(declval<T>()))) {
    T cur = load(Order::Relaxed);
    while (!compare_exchange_weak(cur, f(cur), o, Order::Relaxed)) {
    }
    return cur;
  }

 private:
  static constexpr Order failure_order(Order o) noexcept {
    return o == Order::Release ? Order::Relaxed :
           o == Order::AcqRel ? Order::Acquire : o;
  }

  static constexpr bool valid_failure(Order success, Order failure) noexcept {
    if (failure == Order::Relaxed) return true;
    if (failure == Order::Acquire)
      return success == Order::Acquire || success == Order::AcqRel || success == Order::SeqCst;
    return failure == Order::SeqCst && success == Order::SeqCst;
  }

  static constexpr std::memory_order order(Order o) noexcept {
    return static_cast<std::memory_order>(int(o));
  }

  std::atomic<T> a_{};
};

using AtomicBool = Atomic<bool>;
using AtomicInt = Atomic<int>;
using AtomicU64 = Atomic<uint64>;
using AtomicSize = Atomic<size_t>;
}  // namespace yasl
