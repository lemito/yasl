#pragma once

#include <yasl/time/time.h>

#include <atomic>

#include "futex.h"
#include "mutex.h"

namespace yasl {
// Condition variable. Always wait in a loop or with a predicate.
//
//     auto g = queue.lock();
//     cv.wait(g, [&] { return !g->empty(); });
class CondVar {
 public:
  CondVar() noexcept = default;

  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;

  void notify_one() noexcept {
    seq_.fetch_add(1);
    if (waiters_.load() != 0) {
      detail::futex_wake_one(seq_);
    }
  }

  void notify_all() noexcept {
    seq_.fetch_add(1);
    if (waiters_.load() != 0) {
      detail::futex_wake_all(seq_);
    }
  }

  // m must be locked by the caller; it is unlocked while waiting.
  void wait(Mutex& m) noexcept {
    uint32 seq = seq_.load(std::memory_order_relaxed);
    waiters_.fetch_add(1);
    m.unlock();
    detail::futex_wait(seq_, seq);
    waiters_.fetch_sub(1);
    m.lock();
  }

  // Returns false on timeout.
  bool wait_for(Mutex& m, Duration timeout) noexcept {
    uint32 seq = seq_.load(std::memory_order_relaxed);
    waiters_.fetch_add(1);
    m.unlock();
    bool woken = detail::futex_wait_for(seq_, seq, timeout);
    waiters_.fetch_sub(1);
    m.lock();
    return woken || seq_.load(std::memory_order_relaxed) != seq;
  }

  // `guard` is anything with mutex(), e.g. Guarded<T>::Guard.
  template <class Guard>
  void wait(Guard& guard) noexcept {
    wait(guard.mutex());
  }

  template <class Guard, class Pred>
  void wait(Guard& guard, Pred pred) {
    while (!pred()) {
      wait(guard.mutex());
    }
  }

  // False if the predicate is still false after `timeout`.
  template <class Guard, class Pred>
  bool wait_for(Guard& guard, Duration timeout, Pred pred) {
    Instant deadline = Instant::now() + timeout;
    while (!pred()) {
      Duration left = deadline - Instant::now();
      if (left <= Duration() || !wait_for(guard.mutex(), left)) {
        return pred();
      }
    }
    return true;
  }

 private:
  std::atomic<uint32> seq_{0};
  std::atomic<uint32> waiters_{0};
};
}  // namespace yasl
