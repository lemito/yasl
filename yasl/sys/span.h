#pragma once

#include "check.h"
#include "utility.h"

namespace yasl {
    // Non-owning view of a contiguous array.
    template <class T>
    class Span {
    public:
        using value_type = RemoveCV<T>;
        using iterator = T*;

        constexpr Span() noexcept = default;

        constexpr Span(T* data, size_t size) noexcept
            : p_(data)
            , n_(size)
        {
        }

        template <size_t N>
        constexpr Span(T (&array)[N]) noexcept
            : p_(array)
            , n_(N)
        {
        }

        // Any contiguous container: Vector, String, ...
        template <class C>
            requires(!SameAs<RemoveCV<C>, Span> && requires(C& c) {
                { c.data() } -> ConvertibleTo<T*>;
                { c.size() } -> ConvertibleTo<size_t>;
            })
        constexpr Span(C& c) noexcept
            : p_(c.data())
            , n_(c.size())
        {
        }

        template <class U>
            requires(!SameAs<U, T> && ConvertibleTo<U (*)[], T (*)[]>)
        constexpr Span(Span<U> other) noexcept
            : p_(other.data())
            , n_(other.size())
        {
        }

        constexpr T* data() const noexcept {
            return p_;
        }

        constexpr size_t size() const noexcept {
            return n_;
        }

        constexpr bool empty() const noexcept {
            return n_ == 0;
        }

        constexpr T* begin() const noexcept {
            return p_;
        }

        constexpr T* end() const noexcept {
            return p_ + n_;
        }

        constexpr T& operator[](size_t i) const noexcept {
            YASL_ASSERT(i < n_);
            return p_[i];
        }

        constexpr Span first(size_t n) const noexcept {
            return {p_, yasl::min(n, n_)};
        }

        constexpr Span last(size_t n) const noexcept {
            n = yasl::min(n, n_);
            return {p_ + n_ - n, n};
        }

        constexpr Span subspan(size_t pos, size_t n = size_t(-1)) const noexcept {
            pos = yasl::min(pos, n_);
            return {p_ + pos, yasl::min(n, n_ - pos)};
        }

    private:
        T* p_ = nullptr;
        size_t n_ = 0;
    };
}
