#pragma once

#include <yasl/mem/resource.h>
#include <yasl/sys/check.h>
#include <yasl/sys/utility.h>

namespace yasl {
    // Sole owner of an object allocated from a mem::Resource (unique_ptr / Rust Box).
    // The destroy function remembers the exact allocated type, so Unique<Derived> converts
    // to Unique<Base> when Base has a virtual destructor.
    // Only single (non-virtual) inheritance upcasts are supported: the pointer must not move.
    //
    //     auto shape = make_unique<Circle>(1.0);
    //     Unique<Shape> base = yasl::move(shape);
    template <class T>
    class Unique {
    public:
        Unique() noexcept = default;

        Unique(decltype(nullptr)) noexcept {
        }

        Unique(const Unique&) = delete;
        Unique& operator=(const Unique&) = delete;

        Unique(Unique&& other) noexcept
            : p_(yasl::exchange(other.p_, nullptr))
            , res_(other.res_)
            , drop_(other.drop_)
        {
        }

        template <class U>
            requires(!SameAs<U, T> && DerivedFrom<U, T> && HasVirtualDestructor<T>)
        Unique(Unique<U>&& other) noexcept
            : p_(other.p_)
            , res_(other.res_)
            , drop_(other.drop_)
        {
            YASL_ASSERT(static_cast<const void*>(p_) == static_cast<const void*>(other.p_));
            other.p_ = nullptr;
        }

        ~Unique() {
            reset();
        }

        Unique& operator=(Unique&& other) noexcept {
            if (this != &other) {
                reset();
                p_ = yasl::exchange(other.p_, nullptr);
                res_ = other.res_;
                drop_ = other.drop_;
            }
            return *this;
        }

        Unique& operator=(decltype(nullptr)) noexcept {
            reset();
            return *this;
        }

        template <class U, class... Args>
        static Unique make_in(mem::Resource* res, Args&&... args) {
            static_assert(SameAs<U, T> || (DerivedFrom<U, T> && HasVirtualDestructor<T>),
                "Unique<T> can hold a derived type only if T has a virtual destructor");
            Unique out;
            out.p_ = res->create<U>(yasl::forward<Args>(args)...);
            out.res_ = res;
            out.drop_ = [](mem::Resource* r, void* p) noexcept { r->destroy(static_cast<U*>(p)); };
            return out;
        }

        T* get() const noexcept {
            return p_;
        }

        T& operator*() const noexcept {
            YASL_ASSERT(p_);
            return *p_;
        }

        T* operator->() const noexcept {
            YASL_ASSERT(p_);
            return p_;
        }

        explicit operator bool() const noexcept {
            return p_ != nullptr;
        }

        mem::Resource* resource() const noexcept {
            return res_;
        }

        // Gives up ownership: the caller must destroy the object through the returned Resource.
        Pair<T*, mem::Resource*> release() noexcept {
            return {yasl::exchange(p_, nullptr), res_};
        }

        void reset() noexcept {
            if (p_) {
                drop_(res_, p_);
                p_ = nullptr;
            }
        }

        void swap(Unique& other) noexcept {
            yasl::swap(p_, other.p_);
            yasl::swap(res_, other.res_);
            yasl::swap(drop_, other.drop_);
        }

        friend bool operator==(const Unique& a, const Unique& b) noexcept {
            return a.p_ == b.p_;
        }

        friend bool operator==(const Unique& a, decltype(nullptr)) noexcept {
            return a.p_ == nullptr;
        }

    private:
        template <class U>
        friend class Unique;

        T* p_ = nullptr;
        mem::Resource* res_ = nullptr;
        void (*drop_)(mem::Resource*, void*) noexcept = nullptr;
    };

    template <class T, class... Args>
    Unique<T> make_unique(Args&&... args) {
        return Unique<T>::template make_in<T>(mem::default_resource(), yasl::forward<Args>(args)...);
    }

    template <class T, class... Args>
    Unique<T> make_unique_in(mem::Resource* res, Args&&... args) {
        return Unique<T>::template make_in<T>(res, yasl::forward<Args>(args)...);
    }

    // Allocates a U but returns it as Unique<T> (T = base class of U).
    template <class T, class U, class... Args>
    Unique<T> make_unique_as(mem::Resource* res, Args&&... args) {
        return Unique<T>::template make_in<U>(res, yasl::forward<Args>(args)...);
    }
}
