#pragma once

#include "span.h"
#include "types.h"
#include "hash.h"
#include "utility.h"

namespace yasl {
    // 64 random bits from the OS (getrandom, falling back to /dev/urandom).
    uint64 os_random_u64() noexcept;

    // Random value fixed for the lifetime of the process; seeds the hash tables against HashDoS.
    uint64 process_seed() noexcept;

    // xoshiro256** pseudo-random generator: fast, 256 bits of state, NOT cryptographic.
    //
    //     Random rng = Random::from_os();
    //     int roll = int(rng.range(1, 7));      // 1..6
    //     rng.shuffle(Span<int>(cards));
    class Random {
    public:
        // Deterministic sequence for a given seed (expanded with splitmix64).
        explicit Random(uint64 seed) noexcept {
            for (uint64& w : s_) {
                seed += 0x9E3779B97F4A7C15ull;
                w = mix64(seed);
            }
        }

        static Random from_os() noexcept {
            return Random(os_random_u64());
        }

        uint64 next_u64() noexcept {
            uint64 result = rotl(s_[1] * 5, 7) * 9;
            uint64 t = s_[1] << 17;
            s_[2] ^= s_[0];
            s_[3] ^= s_[1];
            s_[1] ^= s_[2];
            s_[0] ^= s_[3];
            s_[2] ^= t;
            s_[3] = rotl(s_[3], 45);
            return result;
        }

        uint32 next_u32() noexcept {
            return uint32(next_u64() >> 32);
        }

        // Uniform in [0, 1).
        double next_double() noexcept {
            return double(next_u64() >> 11) * 0x1.0p-53;
        }

        bool next_bool() noexcept {
            return (next_u64() >> 63) != 0;
        }

        // Uniform in [0, n) without modulo bias (Lemire's method). n > 0.
        uint64 below(uint64 n) noexcept {
            YASL_ASSERT(n > 0);
            uint64 hi;
            uint64 lo = mul128(next_u64(), n, hi);
            if (lo < n) {
                uint64 threshold = (uint64(0) - n) % n;
                while (lo < threshold) {
                    lo = mul128(next_u64(), n, hi);
                }
            }
            return hi;
        }

        // Uniform in [lo, hi). lo < hi.
        template <Integral T>
        T range(T lo, T hi) noexcept {
            YASL_ASSERT(lo < hi);
            return T(int64(lo) + int64(below(uint64(hi) - uint64(lo))));
        }

        // Fisher-Yates.
        template <class T>
        void shuffle(Span<T> items) noexcept {
            for (size_t i = items.size(); i > 1; --i) {
                yasl::swap(items[i - 1], items[size_t(below(i))]);
            }
        }

    private:
        // Full 64x64 -> 128 bit product: returns the low half, the high half goes to `hi`.
        static constexpr uint64 mul128(uint64 a, uint64 b, uint64& hi) noexcept {
            uint64 a0 = a & 0xFFFFFFFFull, a1 = a >> 32;
            uint64 b0 = b & 0xFFFFFFFFull, b1 = b >> 32;
            uint64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
            uint64 mid = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);
            hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
            return (mid << 32) | (p00 & 0xFFFFFFFFull);
        }

        static constexpr uint64 rotl(uint64 x, int k) noexcept {
            return (x << k) | (x >> (64 - k));
        }

        uint64 s_[4];
    };
}
