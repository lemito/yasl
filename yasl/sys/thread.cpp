#include "thread.h"

#include <sched.h>
#include <time.h>
#include <unistd.h>

namespace yasl {
    namespace {
        struct Start {
            Function<void()> fn;
            mem::Resource* res;
        };

        void* trampoline(void* arg) {
            Start* s = static_cast<Start*>(arg);
            Function<void()> fn = yasl::move(s->fn);
            s->res->destroy(s);
            fn();
            return nullptr;
        }
    }

    Thread::Thread(Function<void()>&& f, mem::Resource* res) {
        Start* s = res->create<Start>(Start{yasl::move(f), res});
        if (pthread_create(&id_, nullptr, &trampoline, s) != 0) {
            res->destroy(s);
            throw Exception("yasl::Thread: cannot start a thread");
        }
        joinable_ = true;
    }

    unsigned Thread::hardware_concurrency() noexcept {
        long n = ::sysconf(_SC_NPROCESSORS_ONLN);
        return n > 0 ? unsigned(n) : 1u;
    }

    namespace this_thread {
        void sleep_for(Duration d) noexcept {
            int64 ns = d.as_nanos();
            if (ns <= 0) {
                return;
            }
            timespec ts;
            ts.tv_sec = time_t(ns / 1'000'000'000);
            ts.tv_nsec = long(ns % 1'000'000'000);
            while (::nanosleep(&ts, &ts) != 0) {
                // interrupted by a signal: sleep the rest
            }
        }

        void yield() noexcept {
            ::sched_yield();
        }

        uint64 id() noexcept {
            return uint64(reinterpret_cast<uintptr_t>(pthread_self()));
        }
    }
}
