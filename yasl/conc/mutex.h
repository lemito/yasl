#pragma once

#include <yasl/core/types.h>

namespace yasl {
namespace detail {
// The OS lock lives in inline storage so that no system header is needed here;
// the size is checked against the real type in mutex.cpp. This is a pImpl
// without the allocation.
#if defined(__APPLE__)
inline constexpr size_t kMutexSize = 64;
inline constexpr size_t kRwLockSize = 200;
#else
inline constexpr size_t kMutexSize = 40;
inline constexpr size_t kRwLockSize = 56;
#endif
}  // namespace detail

// Thin wrapper over the OS mutex.
class Mutex {
 public:
  Mutex() noexcept;
  ~Mutex();

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  void lock() noexcept;
  bool try_lock() noexcept;
  void unlock() noexcept;

  // Pointer to the underlying pthread_mutex_t.
  void* native_handle() noexcept { return storage_; }

 private:
  alignas(8) unsigned char storage_[detail::kMutexSize];
};

// Thin wrapper over the OS reader-writer lock; see RwLock<T> for the typed
// version.
class RawRwLock {
 public:
  RawRwLock() noexcept;
  ~RawRwLock();

  RawRwLock(const RawRwLock&) = delete;
  RawRwLock& operator=(const RawRwLock&) = delete;

  void lock_shared() noexcept;
  void lock() noexcept;
  bool try_lock_shared() noexcept;
  bool try_lock() noexcept;
  void unlock() noexcept;

 private:
  alignas(8) unsigned char storage_[detail::kRwLockSize];
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
