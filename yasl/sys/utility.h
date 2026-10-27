#pragma once

#include "bits.h"
#include "traits.h"

#include <new> // placement new: language support

// Note for library code: always call these qualified (yasl::move, yasl::forward, ...).
// Unqualified calls would also find std::move via ADL for std types and become ambiguous.

namespace yasl {
    template <class T>
    constexpr RemoveReference<T>&& move(T&& v) noexcept {
        return static_cast<RemoveReference<T>&&>(v);
    }

    template <class T>
    constexpr T&& forward(RemoveReference<T>& v) noexcept {
        return static_cast<T&&>(v);
    }

    template <class T>
    constexpr T&& forward(RemoveReference<T>&& v) noexcept {
        return static_cast<T&&>(v);
    }

    template <class T, class U = T>
    constexpr T exchange(T& obj, U&& value) {
        T old = yasl::move(obj);
        obj = yasl::forward<U>(value);
        return old;
    }

    template <class T>
    constexpr void swap(T& a, T& b) noexcept(NothrowMoveConstructible<T>) {
        T tmp = yasl::move(a);
        a = yasl::move(b);
        b = yasl::move(tmp);
    }

    template <class T>
    constexpr T* addressof(T& v) noexcept {
        return __builtin_addressof(v);
    }

    template <class T>
    constexpr T* launder(T* p) noexcept {
        return __builtin_launder(p);
    }

    template <class To, class From>
    constexpr To bit_cast(const From& v) noexcept {
        static_assert(sizeof(To) == sizeof(From));
        return __builtin_bit_cast(To, v);
    }

    template <class T>
    constexpr const T& min(const T& a, const T& b) noexcept {
        return b < a ? b : a;
    }

    template <class T>
    constexpr const T& max(const T& a, const T& b) noexcept {
        return a < b ? b : a;
    }

    template <class T>
    constexpr const T& clamp(const T& v, const T& lo, const T& hi) noexcept {
        return v < lo ? lo : (hi < v ? hi : v);
    }

    // Object lifetime

    template <class T, class... Args>
    T* construct_at(T* p, Args&&... args) {
        return ::new (static_cast<void*>(p)) T(yasl::forward<Args>(args)...);
    }

    template <class T>
    void destroy_at(T* p) noexcept {
        p->~T();
    }

    template <class T>
    void destroy_n(T* p, size_t n) noexcept {
        if constexpr (!TriviallyDestructible<T>) {
            for (size_t i = 0; i < n; ++i) {
                p[i].~T();
            }
        }
    }

    template <class T>
    void destroy(T* first, T* last) noexcept {
        destroy_n(first, size_t(last - first));
    }

    // Raw memory (the builtins lower to the C runtime)

    inline void* mem_copy(void* dst, const void* src, size_t n) noexcept {
        return __builtin_memcpy(dst, src, n);
    }

    inline void* mem_move(void* dst, const void* src, size_t n) noexcept {
        return __builtin_memmove(dst, src, n);
    }

    inline void* mem_set(void* dst, int c, size_t n) noexcept {
        return __builtin_memset(dst, c, n);
    }

    constexpr int mem_compare(const void* a, const void* b, size_t n) noexcept {
        return __builtin_memcmp(a, b, n);
    }

    constexpr size_t str_length(const char* s) noexcept {
        return __builtin_strlen(s);
    }

    // Numeric limits

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T max_value() noexcept {
        if constexpr (SignedIntegral<T>) {
            return T(MakeUnsigned<T>(~MakeUnsigned<T>(0)) >> 1);
        } else {
            return T(~T(0));
        }
    }

    template <Integral T>
        requires(!SameAs<RemoveCV<T>, bool>)
    constexpr T min_value() noexcept {
        if constexpr (SignedIntegral<T>) {
            return T(-max_value<T>() - 1);
        } else {
            return T(0);
        }
    }

    // Function objects

    // Transparent "<": allows heterogeneous lookup (Map<String, ...>::find("text")).
    struct Less {
        template <class A, class B>
        constexpr bool operator()(const A& a, const B& b) const noexcept(noexcept(a < b)) {
            return a < b;
        }
    };

    struct Identity {
        template <class T>
        constexpr const T& operator()(const T& v) const noexcept {
            return v;
        }
    };

    template <class A, class B>
    struct Pair {
        A first;
        B second;

        friend bool operator==(const Pair&, const Pair&) = default;
        friend auto operator<=>(const Pair&, const Pair&) = default;
    };

    template <class A, class B>
    Pair(A, B) -> Pair<A, B>;
}
