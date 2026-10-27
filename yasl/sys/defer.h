#pragma once

#include "utility.h"

namespace yasl {
    // Runs a callback when it goes out of scope (Go `defer`, Rust scope guard).
    //
    //     auto g = scope_exit([&] { rollback(); });
    //     ...
    //     g.dismiss();                  // success: skip the rollback
    template <class F>
    class ScopeExit {
    public:
        explicit ScopeExit(F f) noexcept(NothrowMoveConstructible<F>)
            : f_(yasl::move(f))
        {
        }

        ScopeExit(ScopeExit&& other) noexcept(NothrowMoveConstructible<F>)
            : f_(yasl::move(other.f_))
            , active_(yasl::exchange(other.active_, false))
        {
        }

        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;
        ScopeExit& operator=(ScopeExit&&) = delete;

        ~ScopeExit() {
            if (active_) {
                f_();
            }
        }

        void dismiss() noexcept {
            active_ = false;
        }

    private:
        F f_;
        bool active_ = true;
    };

    template <class F>
    ScopeExit<RemoveCVRef<F>> scope_exit(F&& f) {
        return ScopeExit<RemoveCVRef<F>>(yasl::forward<F>(f));
    }

    namespace detail {
        struct DeferTag {};

        template <class F>
        ScopeExit<F> operator+(DeferTag, F f) {
            return ScopeExit<F>(yasl::move(f));
        }
    }
}

#define YASL_DEFER_CAT2(a, b) a##b
#define YASL_DEFER_CAT(a, b) YASL_DEFER_CAT2(a, b)

// Runs the block at scope exit, in reverse order of declaration.
//
//     YASL_DEFER { close(fd); };
#define YASL_DEFER auto YASL_DEFER_CAT(yasl_defer_, __LINE__) = ::yasl::detail::DeferTag{} + [&]()
