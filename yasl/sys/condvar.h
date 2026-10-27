#pragma once

#include "mutex.h"
#include "time.h"

#include <pthread.h>

namespace yasl {
    // Condition variable (monotonic clock for timeouts). Always wait in a loop or with a predicate.
    //
    //     auto g = queue.lock();
    //     cv.wait(g, [&] { return !g->empty(); });
    class CondVar {
    public:
        CondVar() noexcept;
        ~CondVar();

        CondVar(const CondVar&) = delete;
        CondVar& operator=(const CondVar&) = delete;

        void notify_one() noexcept {
            pthread_cond_signal(&cv_);
        }

        void notify_all() noexcept {
            pthread_cond_broadcast(&cv_);
        }

        // m must be locked by the caller; it is unlocked while waiting.
        void wait(Mutex& m) noexcept;

        // Returns false on timeout.
        bool wait_for(Mutex& m, Duration timeout) noexcept;

        // `guard` is anything with mutex(), e.g. Guarded<T>::Guard.
        template <class Guard>
        void wait(Guard& guard) noexcept {
            wait(guard.mutex());
        }

        template <class Guard, class Pred>
        void wait(Guard& guard, Pred pred) {
            while (!pred()) {
                wait(guard.mutex());
            }
        }

        // False if the predicate is still false after `timeout`.
        template <class Guard, class Pred>
        bool wait_for(Guard& guard, Duration timeout, Pred pred) {
            Instant deadline = Instant::now() + timeout;
            while (!pred()) {
                Duration left = deadline - Instant::now();
                if (left <= Duration() || !wait_for(guard.mutex(), left)) {
                    return pred();
                }
            }
            return true;
        }

    private:
        pthread_cond_t cv_;
    };
}
