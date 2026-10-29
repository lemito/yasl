#include "futex.h"

#if defined(YASL_FUTEX_PTHREAD)
#include <errno.h>
#include <pthread.h>
#include <time.h>
#elif defined(__linux__)
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <errno.h>
#elif defined(_WIN32)
#include <windows.h>
#if defined(_MSC_VER)
#pragma comment(lib, "synchronization.lib")
#endif
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

#if !defined(YASL_FUTEX_PTHREAD) && defined(__linux__) && \
    !defined(SYS_futex) && defined(SYS_futex_time64)
#define SYS_futex SYS_futex_time64
#endif

#if !defined(YASL_FUTEX_PTHREAD) && defined(__APPLE__)
extern "C" int __ulock_wait(uint32_t operation, void* addr, uint64_t value,
                            uint32_t timeout_us);
extern "C" int __ulock_wake(uint32_t operation, void* addr,
                            uint64_t wake_value);
#endif

namespace yasl::detail {
namespace {
uint32* addr_of(std::atomic<uint32>& word) noexcept {
  return reinterpret_cast<uint32*>(&word);
}
}  // namespace

#if defined(YASL_FUTEX_PTHREAD)
#elif defined(__linux__)

namespace {
long sys_futex(std::atomic<uint32>& word, int op, uint32 value,
               const timespec* timeout) noexcept {
  return ::syscall(SYS_futex, addr_of(word), op, value, timeout, nullptr, 0);
}
}  // namespace

void futex_wait(std::atomic<uint32>& word, uint32 expected) noexcept {
  sys_futex(word, FUTEX_WAIT_PRIVATE, expected, nullptr);
}

bool futex_wait_for(std::atomic<uint32>& word, uint32 expected,
                    Duration timeout) noexcept {
  int64 ns = timeout.as_nanos();
  if (ns <= 0) {
    return false;
  }
  timespec ts;
  ts.tv_sec = time_t(ns / 1'000'000'000);
  ts.tv_nsec = long(ns % 1'000'000'000);
  return sys_futex(word, FUTEX_WAIT_PRIVATE, expected, &ts) == 0 ||
         errno != ETIMEDOUT;
}

void futex_wake_one(std::atomic<uint32>& word) noexcept {
  sys_futex(word, FUTEX_WAKE_PRIVATE, 1, nullptr);
}

void futex_wake_all(std::atomic<uint32>& word) noexcept {
  sys_futex(word, FUTEX_WAKE_PRIVATE, 0x7FFFFFFF, nullptr);
}

#elif defined(__APPLE__)

namespace {
constexpr uint32 kCompareAndWait = 1;
constexpr uint32 kWakeAll = 0x100;
constexpr uint32 kNoErrno = 0x01000000;
}  // namespace

void futex_wait(std::atomic<uint32>& word, uint32 expected) noexcept {
  ::__ulock_wait(kCompareAndWait | kNoErrno, addr_of(word), expected, 0);
}

bool futex_wait_for(std::atomic<uint32>& word, uint32 expected,
                    Duration timeout) noexcept {
  int64 ns = timeout.as_nanos();
  if (ns <= 0) {
    return false;
  }
  int64 us = (ns + 999) / 1000;
  uint32 limit = us > int64(0xFFFFFFFF) ? 0xFFFFFFFFu : uint32(us);
  return ::__ulock_wait(kCompareAndWait | kNoErrno, addr_of(word), expected,
                        limit) != -ETIMEDOUT;
}

void futex_wake_one(std::atomic<uint32>& word) noexcept {
  ::__ulock_wake(kCompareAndWait | kNoErrno, addr_of(word), 0);
}

void futex_wake_all(std::atomic<uint32>& word) noexcept {
  ::__ulock_wake(kCompareAndWait | kWakeAll | kNoErrno, addr_of(word), 0);
}

#elif defined(_WIN32)

void futex_wait(std::atomic<uint32>& word, uint32 expected) noexcept {
  ::WaitOnAddress(addr_of(word), &expected, sizeof(expected), INFINITE);
}

bool futex_wait_for(std::atomic<uint32>& word, uint32 expected,
                    Duration timeout) noexcept {
  int64 ns = timeout.as_nanos();
  if (ns <= 0) {
    return false;
  }
  int64 ms = (ns + 999'999) / 1'000'000;
  DWORD limit = ms >= int64(INFINITE) ? INFINITE - 1 : DWORD(ms);
  return ::WaitOnAddress(addr_of(word), &expected, sizeof(expected), limit) ||
         ::GetLastError() != ERROR_TIMEOUT;
}

void futex_wake_one(std::atomic<uint32>& word) noexcept {
  ::WakeByAddressSingle(addr_of(word));
}

void futex_wake_all(std::atomic<uint32>& word) noexcept {
  ::WakeByAddressAll(addr_of(word));
}

#endif

#if defined(YASL_FUTEX_PTHREAD) || \
    !(defined(__linux__) || defined(__APPLE__) || defined(_WIN32))

namespace {
struct Bucket {
  pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t c = PTHREAD_COND_INITIALIZER;
};

Bucket g_buckets[64];

Bucket& bucket_of(std::atomic<uint32>& word) noexcept {
  return g_buckets[(reinterpret_cast<uintptr_t>(&word) >> 4) % 64];
}
}  // namespace

void futex_wait(std::atomic<uint32>& word, uint32 expected) noexcept {
  Bucket& b = bucket_of(word);
  pthread_mutex_lock(&b.m);
  if (word.load() == expected) {
    pthread_cond_wait(&b.c, &b.m);
  }
  pthread_mutex_unlock(&b.m);
}

bool futex_wait_for(std::atomic<uint32>& word, uint32 expected,
                    Duration timeout) noexcept {
  int64 ns = timeout.as_nanos();
  if (ns <= 0) {
    return false;
  }
  timespec now;
  ::clock_gettime(CLOCK_REALTIME, &now);
  int64 total = int64(now.tv_nsec) + ns % 1'000'000'000;
  timespec deadline;
  deadline.tv_sec =
      now.tv_sec + time_t(ns / 1'000'000'000 + total / 1'000'000'000);
  deadline.tv_nsec = long(total % 1'000'000'000);
  Bucket& b = bucket_of(word);
  int r = 0;
  pthread_mutex_lock(&b.m);
  if (word.load() == expected) {
    r = pthread_cond_timedwait(&b.c, &b.m, &deadline);
  }
  pthread_mutex_unlock(&b.m);
  return r != ETIMEDOUT;
}

void futex_wake_one(std::atomic<uint32>& word) noexcept {
  futex_wake_all(word);
}

void futex_wake_all(std::atomic<uint32>& word) noexcept {
  Bucket& b = bucket_of(word);
  pthread_mutex_lock(&b.m);
  pthread_mutex_unlock(&b.m);
  pthread_cond_broadcast(&b.c);
}

#endif
}  // namespace yasl::detail
