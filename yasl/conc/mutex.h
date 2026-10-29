#pragma once

#include <yasl/core/types.h>

#include <atomic>

#include "semaphore.h"

namespace yasl {
class Mutex {
 public:
  Mutex() noexcept = default;

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  void lock() noexcept { sem_.acquire(); }
  bool try_lock() noexcept { return sem_.try_acquire(); }
  void unlock() noexcept { sem_.release(); }

 private:
  Semaphore sem_{1};
};

// See RwLock<T> for the typed version.
class RawRwLock {
 public:
  RawRwLock() noexcept = default;

  RawRwLock(const RawRwLock&) = delete;
  RawRwLock& operator=(const RawRwLock&) = delete;

  void lock_shared() noexcept;
  void lock() noexcept;
  bool try_lock_shared() noexcept;
  bool try_lock() noexcept;
  void unlock() noexcept;

 private:
  std::atomic<uint32> state_{0};
  std::atomic<uint32> waiters_{0};
};

template <class M>
class LockGuard {
 public:
  explicit LockGuard(M& m) noexcept : m_(m) { m_.lock(); }

  ~LockGuard() { m_.unlock(); }

  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  M& m_;
};
}  // namespace yasl
