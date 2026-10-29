#include "mutex.h"

namespace yasl {
namespace {
constexpr uint32 kWriter = 1u << 31;
constexpr uint32 kWriterWaiting = 1u << 30;
}  // namespace

bool RawRwLock::try_lock_shared() noexcept {
  uint32 s = state_.load(std::memory_order_relaxed);
  while ((s & (kWriter | kWriterWaiting)) == 0) {
    if (state_.compare_exchange_weak(s, s + 1, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

bool RawRwLock::try_lock() noexcept {
  uint32 s = state_.load(std::memory_order_relaxed);
  while ((s & ~kWriterWaiting) == 0) {
    if (state_.compare_exchange_weak(s, kWriter, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

void RawRwLock::lock_shared() noexcept {
  while (!try_lock_shared()) {
    uint32 s = state_.load(std::memory_order_relaxed);
    if ((s & (kWriter | kWriterWaiting)) == 0) {
      continue;
    }
    waiters_.fetch_add(1);
    detail::futex_wait(state_, s);
    waiters_.fetch_sub(1);
  }
}

void RawRwLock::lock() noexcept {
  while (!try_lock()) {
    uint32 s = state_.load(std::memory_order_relaxed);
    if ((s & ~kWriterWaiting) == 0) {
      continue;
    }
    if ((s & kWriterWaiting) == 0) {
      state_.compare_exchange_weak(s, s | kWriterWaiting,
                                   std::memory_order_relaxed);
      continue;
    }
    waiters_.fetch_add(1);
    detail::futex_wait(state_, s);
    waiters_.fetch_sub(1);
  }
}

void RawRwLock::unlock() noexcept {
  if (state_.load(std::memory_order_relaxed) & kWriter) {
    state_.fetch_and(~kWriter);
  } else {
    state_.fetch_sub(1);
  }
  if (waiters_.load() != 0) {
    detail::futex_wake_all(state_);
  }
}
}  // namespace yasl
