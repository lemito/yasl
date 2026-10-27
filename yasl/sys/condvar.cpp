#include "condvar.h"

#include <time.h>

namespace yasl {
    CondVar::CondVar() noexcept {
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(&cv_, &attr);
        pthread_condattr_destroy(&attr);
    }

    CondVar::~CondVar() {
        pthread_cond_destroy(&cv_);
    }

    void CondVar::wait(Mutex& m) noexcept {
        pthread_cond_wait(&cv_, m.native_handle());
    }

    bool CondVar::wait_for(Mutex& m, Duration timeout) noexcept {
        timespec now;
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        int64 ns = int64(now.tv_nsec) + timeout.as_nanos() % 1'000'000'000;
        timespec abs;
        abs.tv_sec = now.tv_sec + time_t(timeout.as_secs() + ns / 1'000'000'000);
        abs.tv_nsec = long(ns % 1'000'000'000);
        return pthread_cond_timedwait(&cv_, m.native_handle(), &abs) == 0;
    }
}
