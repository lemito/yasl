#pragma once

#include <yasl/core/traits.h>
#include <yasl/core/utility.h>

#include "view.h"

// Number <-> text conversions, implemented from scratch (no
// printf/strtod/to_chars).
//
//   * integers in any base 2..36;
//   * floating point: shortest round-trip output (Dragon4 on big integers),
//     fixed / scientific output with correct rounding, correctly rounded
//     parsing.

namespace yasl::num {
// Big enough for any 64-bit integer in base 2 plus a sign.
inline constexpr size_t kMaxIntChars = 66;

inline constexpr int kMaxPrecision = 100;

// Big enough for any double in any style with precision <= kMaxPrecision.
inline constexpr size_t kMaxFloatChars = 512;

enum class FloatStyle : uint8 {
  Shortest,    // fewest digits that read back to the same value: 0.1, 1e+100
  Fixed,       // `precision` digits after the point: 3.142
  Scientific,  // `precision` digits after the point: 3.142e+00
};

size_t format_uint(char* buf, uint64 value, uint32 base = 10,
                   bool upper = false) noexcept;
size_t format_int(char* buf, int64 value, uint32 base = 10,
                  bool upper = false) noexcept;

// `buf` must hold kMaxFloatChars. Precision is clamped to [0, kMaxPrecision].
size_t format_double(char* buf, double value,
                     FloatStyle style = FloatStyle::Shortest,
                     int precision = 6) noexcept;
size_t format_float(char* buf, float value,
                    FloatStyle style = FloatStyle::Shortest,
                    int precision = 6) noexcept;

// Parses all of `text` ([+-]digits). False on bad syntax or overflow.
bool parse_uint64(StringView text, uint64& magnitude, bool& negative,
                  uint32 base = 10) noexcept;

// Why parse_uint64 / parse_double rejected `text`.
ParseErrorKind classify_parse_error(StringView text, bool floating,
                                    bool is_unsigned) noexcept;

// Parses all of `text`: [+-]digits[.digits][(e|E)[+-]digits], inf, infinity,
// nan.
bool parse_double(StringView text, double& out) noexcept;
bool parse_float(StringView text, float& out) noexcept;

template <Integral T>
  requires(!SameAs<T, bool>)
bool parse_int(StringView text, T& out, uint32 base = 10) noexcept {
  uint64 mag;
  bool neg;
  if (!parse_uint64(text, mag, neg, base)) {
    return false;
  }
  if constexpr (SignedIntegral<T>) {
    uint64 limit = uint64(max_value<T>()) + (neg ? 1 : 0);
    if (mag > limit) {
      return false;
    }
    out = neg ? T(-int64(mag - 1) - 1) : T(mag);
  } else {
    if ((neg && mag != 0) || mag > uint64(max_value<T>())) {
      return false;
    }
    out = T(mag);
  }
  return true;
}

template <class T>
bool parse(StringView text, T& out) noexcept {
  if constexpr (SameAs<T, double>) {
    return parse_double(text, out);
  } else if constexpr (SameAs<T, float>) {
    return parse_float(text, out);
  } else {
    return parse_int(text, out);
  }
}
}  // namespace yasl::num

namespace yasl {
template <class T>
  requires(Arithmetic<T> && !SameAs<T, bool>)
Optional<T> StringView::parse() const noexcept {
  T value{};
  if (!num::parse(*this, value)) {
    return none;
  }
  return value;
}

template <class T>
  requires(Arithmetic<T> && !SameAs<T, bool>)
Result<T, ParseError> StringView::try_parse() const noexcept {
  T value{};
  if (!num::parse(*this, value)) {
    return err(ParseError(num::classify_parse_error(*this, FloatingPoint<T>,
                                                    UnsignedIntegral<T>)));
  }
  return ok(value);
}
}  // namespace yasl
