#include "mutex.h"

#include <pthread.h>

namespace yasl {
static_assert(sizeof(pthread_mutex_t) <= detail::kMutexSize &&
                  alignof(pthread_mutex_t) <= 8,
              "detail::kMutexSize is too small for pthread_mutex_t");
static_assert(sizeof(pthread_rwlock_t) <= detail::kRwLockSize &&
                  alignof(pthread_rwlock_t) <= 8,
              "detail::kRwLockSize is too small for pthread_rwlock_t");

namespace {
pthread_mutex_t* mutex_of(unsigned char* storage) noexcept {
  return reinterpret_cast<pthread_mutex_t*>(storage);
}

pthread_rwlock_t* rwlock_of(unsigned char* storage) noexcept {
  return reinterpret_cast<pthread_rwlock_t*>(storage);
}
}  // namespace

Mutex::Mutex() noexcept { pthread_mutex_init(mutex_of(storage_), nullptr); }

Mutex::~Mutex() { pthread_mutex_destroy(mutex_of(storage_)); }

void Mutex::lock() noexcept { pthread_mutex_lock(mutex_of(storage_)); }

bool Mutex::try_lock() noexcept {
  return pthread_mutex_trylock(mutex_of(storage_)) == 0;
}

void Mutex::unlock() noexcept { pthread_mutex_unlock(mutex_of(storage_)); }

RawRwLock::RawRwLock() noexcept {
  pthread_rwlock_init(rwlock_of(storage_), nullptr);
}

RawRwLock::~RawRwLock() { pthread_rwlock_destroy(rwlock_of(storage_)); }

void RawRwLock::lock_shared() noexcept {
  pthread_rwlock_rdlock(rwlock_of(storage_));
}

void RawRwLock::lock() noexcept { pthread_rwlock_wrlock(rwlock_of(storage_)); }

bool RawRwLock::try_lock_shared() noexcept {
  return pthread_rwlock_tryrdlock(rwlock_of(storage_)) == 0;
}

bool RawRwLock::try_lock() noexcept {
  return pthread_rwlock_trywrlock(rwlock_of(storage_)) == 0;
}

void RawRwLock::unlock() noexcept {
  pthread_rwlock_unlock(rwlock_of(storage_));
}
}  // namespace yasl
