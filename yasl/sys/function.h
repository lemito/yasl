#pragma once

#include "check.h"
#include "exception.h"
#include "utility.h"

#include <yasl/mem/resource.h>

namespace yasl {
    template <class Sig>
    class FunctionRef;

    // Non-owning reference to a callable, for callback parameters. The callable must outlive the call.
    //
    //     void each(FunctionRef<void(int)> cb);
    //     each([&](int x) { sum += x; });
    template <class R, class... Args>
    class FunctionRef<R(Args...)> {
    public:
        template <class F>
            requires(!SameAs<RemoveCVRef<F>, FunctionRef> && Invocable<F&, Args...>)
        FunctionRef(F&& f) noexcept
            : obj_(const_cast<void*>(static_cast<const void*>(yasl::addressof(f))))
            , call_([](void* o, Args... args) -> R {
                return (*static_cast<RemoveReference<F>*>(o))(static_cast<Args&&>(args)...);
            })
        {
        }

        R operator()(Args... args) const {
            return call_(obj_, static_cast<Args&&>(args)...);
        }

    private:
        void* obj_;
        R (*call_)(void*, Args...);
    };

    template <class Sig>
    class Function;

    // Owning, move-only callable (like std::move_only_function). Callables up to three
    // pointers stay inline, bigger ones are allocated from the Resource.
    //
    //     Function<int(int)> f = [p = make_thing()](int x) { return p->apply(x); };
    template <class R, class... Args>
    class Function<R(Args...)> {
        static constexpr size_t kInline = 3 * sizeof(void*);

        struct Heap {
            void* ptr;
            mem::Resource* res;
        };

        struct Ops {
            R (*call)(void* buf, Args... args);
            void (*move)(void* dst, void* src) noexcept;
            void (*destroy)(void* buf) noexcept;
        };

        template <class F>
        static constexpr bool fits_inline = sizeof(F) <= kInline && alignof(F) <= alignof(void*)
            && NothrowMoveConstructible<F>;

        template <class F, bool Inline>
        struct OpsFor;

    public:
        Function() noexcept = default;

        Function(decltype(nullptr)) noexcept {
        }

        template <class F>
            requires(!SameAs<RemoveCVRef<F>, Function> && Invocable<RemoveCVRef<F>&, Args...>
                && ConstructibleFrom<RemoveCVRef<F>, F&&>)
        Function(F&& f, mem::Resource* res = mem::default_resource()) {
            using D = RemoveCVRef<F>;
            if constexpr (fits_inline<D>) {
                yasl::construct_at(static_cast<D*>(static_cast<void*>(buf_)), yasl::forward<F>(f));
            } else {
                D* p = res->create<D>(yasl::forward<F>(f));
                yasl::construct_at(reinterpret_cast<Heap*>(buf_), Heap{p, res});
            }
            ops_ = &OpsFor<D, fits_inline<D>>::value;
        }

        Function(const Function&) = delete;
        Function& operator=(const Function&) = delete;

        Function(Function&& other) noexcept {
            take(other);
        }

        Function& operator=(Function&& other) noexcept {
            if (this != &other) {
                reset();
                take(other);
            }
            return *this;
        }

        Function& operator=(decltype(nullptr)) noexcept {
            reset();
            return *this;
        }

        ~Function() {
            reset();
        }

        explicit operator bool() const noexcept {
            return ops_ != nullptr;
        }

        R operator()(Args... args) {
            if (!ops_) {
                throw BadAccess("yasl::Function: empty");
            }
            return ops_->call(buf_, static_cast<Args&&>(args)...);
        }

        void reset() noexcept {
            if (ops_) {
                ops_->destroy(buf_);
                ops_ = nullptr;
            }
        }

    private:
        void take(Function& other) noexcept {
            if (other.ops_) {
                other.ops_->move(buf_, other.buf_);
                ops_ = yasl::exchange(other.ops_, nullptr);
            }
        }

        alignas(void*) unsigned char buf_[kInline];
        const Ops* ops_ = nullptr;
    };

    template <class R, class... Args>
    template <class F>
    struct Function<R(Args...)>::OpsFor<F, true> {
        static constexpr Ops value = {
            [](void* b, Args... args) -> R { return (*static_cast<F*>(b))(static_cast<Args&&>(args)...); },
            [](void* dst, void* src) noexcept {
                yasl::construct_at(static_cast<F*>(dst), yasl::move(*static_cast<F*>(src)));
                yasl::destroy_at(static_cast<F*>(src));
            },
            [](void* b) noexcept { yasl::destroy_at(static_cast<F*>(b)); },
        };
    };

    template <class R, class... Args>
    template <class F>
    struct Function<R(Args...)>::OpsFor<F, false> {
        static constexpr Ops value = {
            [](void* b, Args... args) -> R {
                return (*static_cast<F*>(static_cast<Heap*>(b)->ptr))(static_cast<Args&&>(args)...);
            },
            [](void* dst, void* src) noexcept {
                *static_cast<Heap*>(dst) = *static_cast<Heap*>(src);
            },
            [](void* b) noexcept {
                Heap h = *static_cast<Heap*>(b);
                h.res->destroy(static_cast<F*>(h.ptr));
            },
        };
    };
}
