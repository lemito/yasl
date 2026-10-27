#pragma once

#include "mutex.h"
#include "optional.h"
#include "utility.h"

#include <pthread.h>

namespace yasl {
    // A value that can only be reached with its mutex held (Rust Mutex<T>).
    //
    //     Guarded<Vector<int>> items;
    //     items.lock()->push_back(1);
    //     { auto g = items.lock(); g->push_back(2); use(*g); }   // unlocked at the end of the scope
    template <class T>
    class Guarded {
    public:
        class Guard {
        public:
            Guard(const Guard&) = delete;
            Guard& operator=(const Guard&) = delete;

            Guard(Guard&& other) noexcept
                : owner_(yasl::exchange(other.owner_, nullptr))
            {
            }

            ~Guard() {
                if (owner_) {
                    owner_->mutex_.unlock();
                }
            }

            T& operator*() const noexcept {
                return owner_->value_;
            }

            T* operator->() const noexcept {
                return &owner_->value_;
            }

            // For CondVar::wait(guard).
            Mutex& mutex() const noexcept {
                return owner_->mutex_;
            }

        private:
            friend class Guarded;

            explicit Guard(Guarded* owner) noexcept
                : owner_(owner)
            {
            }

            Guarded* owner_;
        };

        Guarded() = default;

        template <class... Args>
            requires(sizeof...(Args) > 0)
        explicit Guarded(Args&&... args)
            : value_(yasl::forward<Args>(args)...)
        {
        }

        Guard lock() noexcept {
            mutex_.lock();
            return Guard(this);
        }

        Optional<Guard> try_lock() noexcept {
            if (mutex_.try_lock()) {
                return Guard(this);
            }
            return none;
        }

        // No locking needed: the caller has exclusive access to the Guarded itself.
        T& get_mut() noexcept {
            return value_;
        }

    private:
        Mutex mutex_;
        T value_{};
    };

    // Reader-writer lock around a value (Rust RwLock<T>): many readers or one writer.
    template <class T>
    class RwLock {
    public:
        class ReadGuard {
        public:
            ReadGuard(const ReadGuard&) = delete;
            ReadGuard& operator=(const ReadGuard&) = delete;

            ReadGuard(ReadGuard&& other) noexcept
                : owner_(yasl::exchange(other.owner_, nullptr))
            {
            }

            ~ReadGuard() {
                if (owner_) {
                    pthread_rwlock_unlock(&owner_->lock_);
                }
            }

            const T& operator*() const noexcept {
                return owner_->value_;
            }

            const T* operator->() const noexcept {
                return &owner_->value_;
            }

        private:
            friend class RwLock;

            explicit ReadGuard(RwLock* owner) noexcept
                : owner_(owner)
            {
            }

            RwLock* owner_;
        };

        class WriteGuard {
        public:
            WriteGuard(const WriteGuard&) = delete;
            WriteGuard& operator=(const WriteGuard&) = delete;

            WriteGuard(WriteGuard&& other) noexcept
                : owner_(yasl::exchange(other.owner_, nullptr))
            {
            }

            ~WriteGuard() {
                if (owner_) {
                    pthread_rwlock_unlock(&owner_->lock_);
                }
            }

            T& operator*() const noexcept {
                return owner_->value_;
            }

            T* operator->() const noexcept {
                return &owner_->value_;
            }

        private:
            friend class RwLock;

            explicit WriteGuard(RwLock* owner) noexcept
                : owner_(owner)
            {
            }

            RwLock* owner_;
        };

        RwLock() = default;

        template <class... Args>
            requires(sizeof...(Args) > 0)
        explicit RwLock(Args&&... args)
            : value_(yasl::forward<Args>(args)...)
        {
        }

        RwLock(const RwLock&) = delete;
        RwLock& operator=(const RwLock&) = delete;

        ~RwLock() {
            pthread_rwlock_destroy(&lock_);
        }

        ReadGuard read() noexcept {
            pthread_rwlock_rdlock(&lock_);
            return ReadGuard(this);
        }

        WriteGuard write() noexcept {
            pthread_rwlock_wrlock(&lock_);
            return WriteGuard(this);
        }

        Optional<ReadGuard> try_read() noexcept {
            if (pthread_rwlock_tryrdlock(&lock_) == 0) {
                return ReadGuard(this);
            }
            return none;
        }

        Optional<WriteGuard> try_write() noexcept {
            if (pthread_rwlock_trywrlock(&lock_) == 0) {
                return WriteGuard(this);
            }
            return none;
        }

    private:
        pthread_rwlock_t lock_ = PTHREAD_RWLOCK_INITIALIZER;
        T value_{};
    };
}
