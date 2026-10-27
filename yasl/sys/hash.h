#pragma once

#include "utility.h"

namespace yasl {
    // splitmix64 finalizer: a cheap, well-mixing 64-bit permutation.
    constexpr uint64 mix64(uint64 x) noexcept {
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    inline uint64 hash_bytes(const void* data, size_t n, uint64 seed = 0) noexcept {
        const auto* p = static_cast<const unsigned char*>(data);
        uint64 h = seed ^ (uint64(n) * 0x9E3779B97F4A7C15ull);
        while (n >= 8) {
            uint64 chunk;
            yasl::mem_copy(&chunk, p, 8);
            h = mix64(h ^ chunk) * 0x9E3779B97F4A7C15ull;
            p += 8;
            n -= 8;
        }
        uint64 tail = 0;
        yasl::mem_copy(&tail, p, n);
        return mix64(h ^ tail);
    }

    // Hash functor: integers and pointers are mixed directly, everything else
    // uses an ADL-found hash_value(const T&).
    template <class T>
    struct Hash {
        uint64 operator()(const T& v) const noexcept {
            if constexpr (Integral<T>) {
                return mix64(uint64(v));
            } else if constexpr (Pointer<T>) {
                return mix64(uint64(reinterpret_cast<uintptr_t>(v)));
            } else {
                return hash_value(v);
            }
        }
    };

    // String literals hash like the String / StringView with the same text.
    template <size_t N>
    struct Hash<char[N]> {
        uint64 operator()(const char (&s)[N]) const noexcept {
            size_t n = 0;
            while (n < N && s[n] != '\0') {
                ++n;
            }
            return hash_bytes(s, n);
        }
    };

    // Transparent hasher / equality for heterogeneous lookup: String and StringView
    // hash alike, so map.get("key"_sv) needs no temporary String.
    struct HashAny {
        template <class Q>
        uint64 operator()(const Q& v) const noexcept {
            return Hash<Q>{}(v);
        }
    };

    struct EqualTo {
        template <class A, class B>
        constexpr bool operator()(const A& a, const B& b) const noexcept(noexcept(a == b)) {
            return a == b;
        }
    };
}
