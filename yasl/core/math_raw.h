#pragma once

#include "traits.h"

// Overflow-aware arithmetic primitives.  This header deliberately has no
// vocabulary or formatting dependencies so that low-level code can use the
// operations without pulling in Optional.
namespace yasl::math {
template <Integral T>
  requires(!SameAs<RemoveCV<T>, bool>)
constexpr bool checked_add(T a, T b, T& out) noexcept {
  return !__builtin_add_overflow(a, b, &out);
}

template <Integral T>
  requires(!SameAs<RemoveCV<T>, bool>)
constexpr bool checked_sub(T a, T b, T& out) noexcept {
  return !__builtin_sub_overflow(a, b, &out);
}

template <Integral T>
  requires(!SameAs<RemoveCV<T>, bool>)
constexpr bool checked_mul(T a, T b, T& out) noexcept {
  return !__builtin_mul_overflow(a, b, &out);
}
}  // namespace yasl::math
