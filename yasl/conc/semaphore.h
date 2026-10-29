#pragma once

#include <yasl/time/time.h>

#include <atomic>

#include "futex.h"

namespace yasl {
// Counting semaphore (Dijkstra / std::counting_semaphore).
//
//     Semaphore slots(4);
//     slots.acquire();      // blocks while all four are taken
//     ...
//     slots.release();
class Semaphore {
 public:
  explicit Semaphore(size_t initial = 0) noexcept : state_(uint32(initial)) {}

  Semaphore(const Semaphore&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;

  // Takes one permit, waiting for one if there is none.
  void acquire() noexcept {
    bool slept = false;
    for (;;) {
      uint32 s = state_.load(std::memory_order_relaxed);
      if (take(s, slept)) {
        return;
      }
      if (mark_waiting(s)) {
        detail::futex_wait(state_, s);
        slept = true;
      }
    }
  }

  bool try_acquire() noexcept {
    uint32 s = state_.load(std::memory_order_relaxed);
    while ((s & kCount) != 0) {
      if (state_.compare_exchange_weak(s, s - 1, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        return true;
      }
    }
    return false;
  }

  // False if no permit showed up within `timeout`.
  bool acquire_for(Duration timeout) noexcept {
    if (try_acquire()) {
      return true;
    }
    Instant deadline = Instant::now() + timeout;
    bool slept = false;
    for (;;) {
      uint32 s = state_.load(std::memory_order_relaxed);
      if (take(s, slept)) {
        return true;
      }
      Duration left = deadline - Instant::now();
      if (left <= Duration()) {
        return false;
      }
      if (mark_waiting(s)) {
        detail::futex_wait_for(state_, s, left);
        slept = true;
      }
    }
  }

  // Adds n permits.
  void release(size_t n = 1) noexcept {
    if (state_.fetch_add(uint32(n), std::memory_order_release) & kWaiters) {
      state_.fetch_and(kCount, std::memory_order_relaxed);
      if (n == 1) {
        detail::futex_wake_one(state_);
      } else {
        detail::futex_wake_all(state_);
      }
    }
  }

 private:
  static constexpr uint32 kWaiters = 1u << 31;
  static constexpr uint32 kCount = kWaiters - 1;

  bool take(uint32& s, bool slept) noexcept {
    while ((s & kCount) != 0) {
      if (state_.compare_exchange_weak(s, (s - 1) | (slept ? kWaiters : 0),
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        return true;
      }
    }
    return false;
  }

  bool mark_waiting(uint32& s) noexcept {
    if (s & kWaiters) {
      return true;
    }
    if (state_.compare_exchange_weak(s, s | kWaiters,
                                     std::memory_order_relaxed)) {
      s |= kWaiters;
      return true;
    }
    return false;
  }

  std::atomic<uint32> state_;
};

// Reusable barrier (std::barrier / Java CyclicBarrier): wait() blocks until
// `parties` threads have called it, then releases them all and resets for the
// next round.
//
//     Barrier b(4);
//     bool leader = b.wait();      // exactly one caller per round gets true
class Barrier {
 public:
  explicit Barrier(size_t parties) noexcept : parties_(uint32(parties)) {}

  Barrier(const Barrier&) = delete;
  Barrier& operator=(const Barrier&) = delete;

  // Returns true for the last thread to arrive.
  bool wait() noexcept {
    uint32 gen = generation_.load();
    if (waiting_.fetch_add(1) + 1 == parties_) {
      waiting_.store(0);
      generation_.fetch_add(1);
      detail::futex_wake_all(generation_);
      return true;
    }
    while (generation_.load() == gen) {
      detail::futex_wait(generation_, gen);
    }
    return false;
  }

  size_t parties() const noexcept { return parties_; }

 private:
  uint32 parties_;
  std::atomic<uint32> waiting_{0};
  std::atomic<uint32> generation_{0};
};
}  // namespace yasl
