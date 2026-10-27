#include "condvar.h"

#include <pthread.h>
#include <time.h>

namespace yasl {
namespace {
pthread_cond_t* cond_of(unsigned char* storage) noexcept {
  return reinterpret_cast<pthread_cond_t*>(storage);
}

pthread_mutex_t* mutex_of(Mutex& m) noexcept {
  return static_cast<pthread_mutex_t*>(m.native_handle());
}
}  // namespace

static_assert(sizeof(pthread_cond_t) <= detail::kCondVarSize &&
                  alignof(pthread_cond_t) <= 8,
              "detail::kCondVarSize is too small for pthread_cond_t");

CondVar::CondVar() noexcept {
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  pthread_cond_init(cond_of(storage_), &attr);
  pthread_condattr_destroy(&attr);
}

CondVar::~CondVar() { pthread_cond_destroy(cond_of(storage_)); }

void CondVar::notify_one() noexcept { pthread_cond_signal(cond_of(storage_)); }

void CondVar::notify_all() noexcept {
  pthread_cond_broadcast(cond_of(storage_));
}

void CondVar::wait(Mutex& m) noexcept {
  pthread_cond_wait(cond_of(storage_), mutex_of(m));
}

bool CondVar::wait_for(Mutex& m, Duration timeout) noexcept {
  timespec now;
  ::clock_gettime(CLOCK_MONOTONIC, &now);
  int64 ns = int64(now.tv_nsec) + timeout.as_nanos() % 1'000'000'000;
  timespec abs;
  abs.tv_sec = now.tv_sec + time_t(timeout.as_secs() + ns / 1'000'000'000);
  abs.tv_nsec = long(ns % 1'000'000'000);
  return pthread_cond_timedwait(cond_of(storage_), mutex_of(m), &abs) == 0;
}
}  // namespace yasl
