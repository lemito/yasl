#pragma once

#include "bits.h"
#include "optional.h"
#include "utility.h"

// Overflow-aware integer arithmetic (Rust checked_* / saturating_* / wrapping_*, C# `checked`).

namespace yasl {
    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr Optional<T> checked_add(T a, T b) noexcept {
        T r;
        if (__builtin_add_overflow(a, b, &r)) {
            return none;
        }
        return r;
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr Optional<T> checked_sub(T a, T b) noexcept {
        T r;
        if (__builtin_sub_overflow(a, b, &r)) {
            return none;
        }
        return r;
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr Optional<T> checked_mul(T a, T b) noexcept {
        T r;
        if (__builtin_mul_overflow(a, b, &r)) {
            return none;
        }
        return r;
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T saturating_add(T a, T b) noexcept {
        T r;
        if (__builtin_add_overflow(a, b, &r)) {
            return (SignedIntegral<T> && b < 0) ? min_value<T>() : max_value<T>();
        }
        return r;
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T saturating_sub(T a, T b) noexcept {
        T r;
        if (__builtin_sub_overflow(a, b, &r)) {
            return (SignedIntegral<T> && b < 0) ? max_value<T>() : min_value<T>();
        }
        return r;
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T saturating_mul(T a, T b) noexcept {
        T r;
        if (__builtin_mul_overflow(a, b, &r)) {
            return ((a < 0) == (b < 0)) ? max_value<T>() : min_value<T>();
        }
        return r;
    }

    // Two's complement wrap-around, no undefined behaviour.
    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T wrapping_add(T a, T b) noexcept {
        using U = MakeUnsigned<T>;
        return T(U(U(a) + U(b)));
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T wrapping_sub(T a, T b) noexcept {
        using U = MakeUnsigned<T>;
        return T(U(U(a) - U(b)));
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T wrapping_mul(T a, T b) noexcept {
        using U = MakeUnsigned<T>;
        return T(U(U(a) * U(b)));
    }

    // Value-preserving conversion, empty if x does not fit into T.
    template <Integral T, Integral U>
        requires(!SameAs<RemoveCV<T>, bool> && !SameAs<RemoveCV<U>, bool>)
    constexpr Optional<T> narrow(U x) noexcept {
        T r = static_cast<T>(x);
        if (static_cast<U>(r) != x || ((r < T(0)) != (x < U(0)))) {
            return none;
        }
        return r;
    }

    // ceil(a / b) for b > 0.
    template <UnsignedIntegral T>
    constexpr T div_ceil(T a, T b) noexcept {
        return T(a / b + (a % b != 0));
    }
}
