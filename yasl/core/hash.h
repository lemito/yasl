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

namespace detail {
constexpr uint64 mum(uint64 a, uint64 b) noexcept {
  WideProduct r = mul_wide(a, b);
  return r.lo ^ r.hi;
}
inline uint64 r8(const unsigned char* p) noexcept {
  uint64 v;
  yasl::mem_copy(&v, p, 8);
  return v;
}
inline uint64 r4(const unsigned char* p) noexcept {
  uint32 v;
  yasl::mem_copy(&v, p, 4);
  return v;
}
}  // namespace detail

inline uint64 hash_bytes(const void* data, size_t n, uint64 seed = 0) noexcept {
  using detail::mum;
  using detail::r4;
  using detail::r8;
  constexpr uint64 k0 = 0xa0761d6478bd642full, k1 = 0xe7037ed1a0b428dbull,
                   k2 = 0x8ebc6af09c88c6e3ull, k3 = 0x589965cc75374cc3ull;
  const auto* p = static_cast<const unsigned char*>(data);
  seed ^= mum(seed ^ k0, k1);
  uint64 a, b;
  if (n <= 16) {
    if (n >= 4) {
      size_t q = (n >> 3) << 2;
      a = (r4(p) << 32) | r4(p + q);
      b = (r4(p + n - 4) << 32) | r4(p + n - 4 - q);
    } else if (n > 0) {
      a = (uint64(p[0]) << 16) | (uint64(p[n >> 1]) << 8) | p[n - 1];
      b = 0;
    } else {
      a = b = 0;
    }
  } else {
    size_t i = n;
    if (i > 48) {
      uint64 s1 = seed, s2 = seed;
      do {
        seed = mum(r8(p) ^ k1, r8(p + 8) ^ seed);
        s1 = mum(r8(p + 16) ^ k2, r8(p + 24) ^ s1);
        s2 = mum(r8(p + 32) ^ k3, r8(p + 40) ^ s2);
        p += 48;
        i -= 48;
      } while (i > 48);
      seed ^= s1 ^ s2;
    }
    while (i > 16) {
      seed = mum(r8(p) ^ k1, r8(p + 8) ^ seed);
      i -= 16;
      p += 16;
    }
    a = r8(p + i - 16);
    b = r8(p + i - 8);
  }
  return mum(k1 ^ uint64(n), mum(a ^ k1, b ^ seed));
}

// Hash functor: integers are passed through, pointers are mixed, everything
// else uses an ADL-found hash_value(const T&).
template <class T>
struct Hash {
  uint64 operator()(const T& v) const noexcept {
    if constexpr (Integral<T>) {
      return uint64(v);
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
  constexpr bool operator()(const A& a, const B& b) const
      noexcept(noexcept(a == b)) {
    return a == b;
  }
};
}  // namespace yasl
