#pragma once

#include <yasl/core/hash.h>
#include <yasl/core/span.h>
#include <yasl/core/types.h>
#include <yasl/core/utility.h>

namespace yasl {
// 64 random bits from the OS (getrandom, falling back to /dev/urandom).
uint64 os_random_u64() noexcept;

// Random value fixed for the lifetime of the process; seeds the hash tables
// against HashDoS.
uint64 process_seed() noexcept;

namespace detail {
// Full 64x64 -> 128 bit product: returns the low half, the high half goes to
// `hi`.
constexpr uint64 mul128(uint64 a, uint64 b, uint64& hi) noexcept {
  uint64 a0 = a & 0xFFFFFFFFull, a1 = a >> 32;
  uint64 b0 = b & 0xFFFFFFFFull, b1 = b >> 32;
  uint64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint64 mid = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);
  hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  return (mid << 32) | (p00 & 0xFFFFFFFFull);
}

// Everything derived from next_u64(); shared by all generators so that they
// have the same API: next_u32, next_double, next_bool, below, range, shuffle.
template <class Derived>
class RandomOps {
 public:
  uint32 next_u32() noexcept { return uint32(self().next_u64() >> 32); }

  // Uniform in [0, 1).
  double next_double() noexcept {
    return double(self().next_u64() >> 11) * 0x1.0p-53;
  }

  bool next_bool() noexcept { return (self().next_u64() >> 63) != 0; }

  // Uniform in [0, n) without modulo bias (Lemire's method). n > 0.
  uint64 below(uint64 n) noexcept {
    YASL_ASSERT(n > 0);
    uint64 hi;
    uint64 lo = mul128(self().next_u64(), n, hi);
    if (lo < n) {
      uint64 threshold = (uint64(0) - n) % n;
      while (lo < threshold) {
        lo = mul128(self().next_u64(), n, hi);
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
  Derived& self() noexcept { return static_cast<Derived&>(*this); }
};
}  // namespace detail

// xoshiro256** pseudo-random generator: fast, 256 bits of state, NOT
// cryptographic.
//
//     Random rng = Random::from_os();
//     int roll = int(rng.range(1, 7));      // 1..6
//     rng.shuffle(Span<int>(cards));
class Random : public detail::RandomOps<Random> {
 public:
  // Deterministic sequence for a given seed (expanded with splitmix64).
  explicit Random(uint64 seed) noexcept {
    for (uint64& w : s_) {
      seed += 0x9E3779B97F4A7C15ull;
      w = mix64(seed);
    }
  }

  static Random from_os() noexcept { return Random(os_random_u64()); }

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

 private:
  static constexpr uint64 rotl(uint64 x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
  }

  uint64 s_[4];
};

// PCG-XSH-RR 32: 64-bit state, 32 bits of output per step, NOT cryptographic.
// `stream` selects one of 2^63 independent sequences.
//
//     Pcg32 rng(42, 54);
//     uint32 x = rng.next_u32();
class Pcg32 : public detail::RandomOps<Pcg32> {
 public:
  explicit Pcg32(uint64 seed, uint64 stream = 0xDA3E39CB94B95BDBull) noexcept {
    inc_ = (stream << 1) | 1;
    step();
    state_ += seed;
    step();
  }

  static Pcg32 from_os() noexcept {
    return Pcg32(os_random_u64(), os_random_u64());
  }

  uint32 next_u32() noexcept {
    uint64 old = state_;
    step();
    uint32 xorshifted = uint32(((old >> 18) ^ old) >> 27);
    uint32 rot = uint32(old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31));
  }

  uint64 next_u64() noexcept {
    uint64 hi = next_u32();
    return (hi << 32) | next_u32();
  }

 private:
  void step() noexcept { state_ = state_ * 6364136223846793005ull + inc_; }

  uint64 state_ = 0;
  uint64 inc_;
};

// PCG-XSL-RR 128/64: 128-bit state, 64 bits of output per step, NOT
// cryptographic.
class Pcg64 : public detail::RandomOps<Pcg64> {
 public:
  explicit Pcg64(uint64 seed, uint64 stream = 0) noexcept {
    inc_ = {(stream << 1) | 1, stream >> 63};
    step();
    add(state_, {seed, 0});
    step();
  }

  static Pcg64 from_os() noexcept {
    return Pcg64(os_random_u64(), os_random_u64());
  }

  uint64 next_u64() noexcept {
    step();
    uint64 folded = state_.hi ^ state_.lo;
    unsigned rot = unsigned(state_.hi >> 58);
    return (folded >> rot) | (folded << ((0u - rot) & 63));
  }

 private:
  struct U128 {
    uint64 lo;
    uint64 hi;
  };

  static void add(U128& a, U128 b) noexcept {
    a.lo += b.lo;
    a.hi += b.hi + (a.lo < b.lo);
  }

  void step() noexcept {
    constexpr U128 kMul = {4865540595714422341ull, 2549297995355413924ull};
    WideProduct p = mul_wide(state_.lo, kMul.lo);
    state_ = {p.lo, p.hi + state_.lo * kMul.hi + state_.hi * kMul.lo};
    add(state_, inc_);
  }

  U128 state_ = {0, 0};
  U128 inc_;
};
}  // namespace yasl
