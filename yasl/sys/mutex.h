#pragma once

#include <pthread.h>

namespace yasl {
    // Thin wrapper over the OS mutex.
    class Mutex {
    public:
        Mutex() noexcept = default;

        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

        ~Mutex() {
            pthread_mutex_destroy(&m_);
        }

        void lock() noexcept {
            pthread_mutex_lock(&m_);
        }

        bool try_lock() noexcept {
            return pthread_mutex_trylock(&m_) == 0;
        }

        void unlock() noexcept {
            pthread_mutex_unlock(&m_);
        }

    private:
        pthread_mutex_t m_ = PTHREAD_MUTEX_INITIALIZER;
    };

    template <class M>
    class LockGuard {
    public:
        explicit LockGuard(M& m) noexcept
            : m_(m)
        {
            m_.lock();
        }

        ~LockGuard() {
            m_.unlock();
        }

        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;

    private:
        M& m_;
    };
}
