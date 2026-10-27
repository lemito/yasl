#pragma once

#include "traits.h"

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
}  // namespace yasl
