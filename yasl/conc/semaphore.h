#pragma once

#include <yasl/time/time.h>

#include "condvar.h"
#include "mutex.h"

namespace yasl {
// Counting semaphore (Dijkstra / std::counting_semaphore) on Mutex + CondVar.
//
//     Semaphore slots(4);
//     slots.acquire();      // blocks while all four are taken
//     ...
//     slots.release();
class Semaphore {
 public:
  explicit Semaphore(size_t initial = 0) noexcept : count_(initial) {}

  Semaphore(const Semaphore&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;

  // Takes one permit, waiting for one if there is none.
  void acquire() {
    LockGuard<Mutex> g(m_);
    while (count_ == 0) {
      cv_.wait(m_);
    }
    --count_;
  }

  bool try_acquire() noexcept {
    LockGuard<Mutex> g(m_);
    if (count_ == 0) {
      return false;
    }
    --count_;
    return true;
  }

  // False if no permit showed up within `timeout`.
  bool acquire_for(Duration timeout) {
    Instant deadline = Instant::now() + timeout;
    LockGuard<Mutex> g(m_);
    while (count_ == 0) {
      Duration left = deadline - Instant::now();
      if (left <= Duration() || !cv_.wait_for(m_, left)) {
        if (count_ == 0) {
          return false;
        }
      }
    }
    --count_;
    return true;
  }

  // Adds n permits.
  void release(size_t n = 1) {
    {
      LockGuard<Mutex> g(m_);
      count_ += n;
    }
    if (n == 1) {
      cv_.notify_one();
    } else {
      cv_.notify_all();
    }
  }

 private:
  Mutex m_;
  CondVar cv_;
  size_t count_;
};

// Reusable barrier (std::barrier / Java CyclicBarrier): wait() blocks until
// `parties` threads have called it, then releases them all and resets for the
// next round.
//
//     Barrier b(4);
//     bool leader = b.wait();      // exactly one caller per round gets true
class Barrier {
 public:
  explicit Barrier(size_t parties) noexcept
      : parties_(parties), waiting_(0), generation_(0) {}

  Barrier(const Barrier&) = delete;
  Barrier& operator=(const Barrier&) = delete;

  // Returns true for the last thread to arrive.
  bool wait() {
    LockGuard<Mutex> g(m_);
    size_t gen = generation_;
    if (++waiting_ == parties_) {
      waiting_ = 0;
      ++generation_;
      cv_.notify_all();
      return true;
    }
    while (gen == generation_) {
      cv_.wait(m_);
    }
    return false;
  }

  size_t parties() const noexcept { return parties_; }

 private:
  Mutex m_;
  CondVar cv_;
  size_t parties_;
  size_t waiting_;
  size_t generation_;
};
}  // namespace yasl
