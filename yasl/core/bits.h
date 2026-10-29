#pragma once

#include "traits.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace yasl {
// Bits

template <UnsignedIntegral T>
constexpr int countl_zero(T x) noexcept {
  constexpr int bits = int(sizeof(T) * 8);
  if (x == 0) {
    return bits;
  }
  if constexpr (sizeof(T) <= 4) {
    return __builtin_clz(uint32(x)) - (32 - bits);
  } else {
    return __builtin_clzll(uint64(x));
  }
}

template <UnsignedIntegral T>
constexpr int bit_width(T x) noexcept {
  return int(sizeof(T) * 8) - countl_zero(x);
}

constexpr uint64 bit_ceil(uint64 x) noexcept {
  return x <= 1 ? 1 : uint64(1) << bit_width(x - 1);
}

template <UnsignedIntegral T>
constexpr int countr_zero(T x) noexcept {
  constexpr int bits = int(sizeof(T) * 8);
  if (x == 0) {
    return bits;
  }
  if constexpr (sizeof(T) <= 4) {
    return __builtin_ctz(uint32(x));
  } else {
    return __builtin_ctzll(uint64(x));
  }
}

template <UnsignedIntegral T>
constexpr int popcount(T x) noexcept {
  if constexpr (sizeof(T) <= 4) {
    return __builtin_popcount(uint32(x));
  } else {
    return __builtin_popcountll(uint64(x));
  }
}

template <UnsignedIntegral T>
constexpr bool is_pow2(T x) noexcept {
  return x != 0 && (x & (x - 1)) == 0;
}

struct WideProduct {
  uint64 lo;
  uint64 hi;
};

constexpr WideProduct mul_wide(uint64 a, uint64 b) noexcept {
  if (!__builtin_is_constant_evaluated()) {
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
    uint64 hi;
    uint64 lo = _umul128(a, b, &hi);
    return {lo, hi};
#elif defined(_MSC_VER) && defined(_M_ARM64) && !defined(__clang__)
    return {a * b, __umulh(a, b)};
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    uint64 lo;
    uint64 hi;
    __asm__("mulq %3" : "=a"(lo), "=d"(hi) : "a"(a), "rm"(b) : "cc");
    return {lo, hi};
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    uint64 hi;
    __asm__("umulh %0, %1, %2" : "=r"(hi) : "r"(a), "r"(b));
    return {a * b, hi};
#endif
  }
  uint64 al = uint32(a);
  uint64 ah = a >> 32;
  uint64 bl = uint32(b);
  uint64 bh = b >> 32;
  uint64 p0 = al * bl;
  uint64 p1 = al * bh;
  uint64 p2 = ah * bl;
  uint64 mid = (p0 >> 32) + uint32(p1) + uint32(p2);
  return {(mid << 32) | uint32(p0),
          ah * bh + (p1 >> 32) + (p2 >> 32) + (mid >> 32)};
}
}  // namespace yasl
