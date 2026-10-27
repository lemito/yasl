#include <yasl/str/builder.h>
#include <yasl/str/num.h>
#include <yasl/test/test.h>

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <string>

// The standard library is used here only as an oracle.

using namespace yasl;
using namespace yasl::literals;

namespace {
String fmt_double(double v, num::FloatStyle style = num::FloatStyle::Shortest,
                  int precision = 6) {
  char buf[num::kMaxFloatChars];
  return String(buf, num::format_double(buf, v, style, precision));
}

// Scientific shortest: fixed notation may print every digit of a large integer.
std::string std_shortest(double v) {
  char buf[64];
  auto r =
      std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::scientific);
  return std::string(buf, r.ptr);
}

// Significant digits of a shortest representation (to compare digit counts).
std::string digits_of(const std::string& s) {
  std::string d;
  for (char c : s) {
    if (c == 'e' || c == 'E') {
      break;
    }
    if (c >= '0' && c <= '9') {
      d += c;
    }
  }
  size_t a = d.find_first_not_of('0');
  if (a == std::string::npos) {
    return "0";
  }
  d = d.substr(a);
  while (d.size() > 1 && d.back() == '0') {
    d.pop_back();
  }
  return d;
}

bool same_bits(double a, double b) {
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}
}  // namespace

YASL_TEST(num_integers) {
  char buf[num::kMaxIntChars];
  CHECK_EQ(StringView(buf, num::format_int(buf, 0)), "0"_sv);
  CHECK_EQ(StringView(buf, num::format_int(buf, -123)), "-123"_sv);
  CHECK_EQ(StringView(buf, num::format_int(buf, min_value<int64>())),
           "-9223372036854775808"_sv);
  CHECK_EQ(StringView(buf, num::format_uint(buf, 255, 16)), "ff"_sv);
  CHECK_EQ(StringView(buf, num::format_uint(buf, 255, 16, true)), "FF"_sv);
  CHECK_EQ(StringView(buf, num::format_uint(buf, 5, 2)), "101"_sv);
  CHECK_EQ(
      StringView(buf, num::format_uint(buf, max_value<uint64>(), 2)).size(),
      size_t(64));

  int x = 0;
  CHECK(num::parse_int("ff"_sv, x, 16) && x == 255);
  CHECK(num::parse_int("-zz"_sv, x, 36) && x == -1295);
  CHECK(!num::parse_int("12"_sv, x, 2));
}

YASL_TEST(num_float_shortest_known) {
  CHECK_EQ(fmt_double(0.1), "0.1");
  CHECK_EQ(fmt_double(0.3), "0.3");
  CHECK_EQ(fmt_double(0.1 + 0.2), "0.30000000000000004");
  CHECK_EQ(fmt_double(1.0), "1");
  CHECK_EQ(fmt_double(-2.5), "-2.5");
  CHECK_EQ(fmt_double(100.0), "100");
  CHECK_EQ(fmt_double(1e21), "1e+21");
  CHECK_EQ(fmt_double(1e100), "1e+100");
  CHECK_EQ(fmt_double(1.5e-7), "1.5e-07");
  CHECK_EQ(fmt_double(0.000123), "0.000123");
  CHECK_EQ(fmt_double(5e-324), "5e-324");
  CHECK_EQ(fmt_double(1.7976931348623157e308), "1.7976931348623157e+308");
  CHECK_EQ(fmt_double(0.0), "0");
  CHECK_EQ(fmt_double(-0.0), "-0");
  CHECK_EQ(fmt_double(std::numeric_limits<double>::infinity()), "inf");
  CHECK_EQ(fmt_double(-std::numeric_limits<double>::infinity()), "-inf");
  CHECK_EQ(fmt_double(std::nan("")), "nan");

  char buf[num::kMaxFloatChars];
  CHECK_EQ(StringView(buf, num::format_float(buf, 0.1f)), "0.1"_sv);
  CHECK_EQ(StringView(buf, num::format_float(buf, 3.4028235e38f)),
           "3.4028235e+38"_sv);
  CHECK_EQ(StringView(buf, num::format_float(buf, 1e-45f)), "1e-45"_sv);
}

YASL_TEST(num_float_fixed_scientific) {
  using num::FloatStyle;
  CHECK_EQ(fmt_double(3.14159, FloatStyle::Fixed, 2), "3.14");
  CHECK_EQ(fmt_double(2.5, FloatStyle::Fixed, 0), "2");
  CHECK_EQ(fmt_double(3.5, FloatStyle::Fixed, 0), "4");
  CHECK_EQ(fmt_double(0.125, FloatStyle::Fixed, 2), "0.12");
  CHECK_EQ(fmt_double(0.001, FloatStyle::Fixed, 5), "0.00100");
  CHECK_EQ(fmt_double(-0.0004, FloatStyle::Fixed, 3), "-0.000");
  CHECK_EQ(fmt_double(1e20, FloatStyle::Fixed, 1), "100000000000000000000.0");
  CHECK_EQ(fmt_double(0.1, FloatStyle::Fixed, 20), "0.10000000000000000555");
  CHECK_EQ(fmt_double(0.0, FloatStyle::Fixed, 3), "0.000");
  CHECK_EQ(fmt_double(123456.0, FloatStyle::Scientific, 2), "1.23e+05");
  CHECK_EQ(fmt_double(9.999, FloatStyle::Scientific, 2), "1.00e+01");
  CHECK_EQ(fmt_double(1e-300, FloatStyle::Scientific, 3), "1.000e-300");
  CHECK_EQ(fmt_double(0.0, FloatStyle::Scientific, 1), "0.0e+00");

  // Cross-check against the standard library on many values.
  std::mt19937_64 rng(5);
  for (int i = 0; i < 3000; ++i) {
    double v = std::ldexp(double(rng() >> 11), int(rng() % 200) - 120) *
               ((rng() & 1) ? 1 : -1);
    int p = int(rng() % 18);
    char buf[512];
    auto r =
        std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, p);
    CHECK_EQ(fmt_double(v, FloatStyle::Fixed, p),
             StringView(buf, size_t(r.ptr - buf)));
    r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::scientific,
                      p);
    CHECK_EQ(fmt_double(v, FloatStyle::Scientific, p),
             StringView(buf, size_t(r.ptr - buf)));
  }
}

YASL_TEST(num_float_roundtrip_random) {
  std::mt19937_64 rng(42);
  for (int i = 0; i < 20000; ++i) {
    double v;
    uint64 bits = rng();
    std::memcpy(&v, &bits, sizeof(v));
    if (!std::isfinite(v)) {
      continue;
    }
    String s = fmt_double(v);
    double back = 0;
    CHECK(num::parse_double(s.view(), back));
    if (!same_bits(v, back)) {
      CHECK_EQ(s, "<round trip failed>");
    }
    // Same number of significant digits as the standard's shortest form.
    std::string ref = std_shortest(v);
    CHECK_EQ(digits_of(std::string(s.data(), s.size())).size(),
             digits_of(ref).size());
  }
}

YASL_TEST(num_float_parse_vs_std) {
  std::mt19937_64 rng(99);
  const char* digits = "0123456789";
  for (int i = 0; i < 20000; ++i) {
    std::string text;
    if (rng() & 1) {
      text += '-';
    }
    int nd = int(rng() % 25) + 1;
    for (int j = 0; j < nd; ++j) {
      text += digits[rng() % 10];
    }
    if (rng() % 3) {
      text.insert(text.size() - size_t(rng() % size_t(nd)), ".");
    }
    text += 'e';
    text += std::to_string(int(rng() % 700) - 350);

    double ours = 0;
    double ref = 0;
    CHECK(num::parse_double(StringView(text), ours));
    auto dres = std::from_chars(text.data(), text.data() + text.size(), ref);
    if (dres.ec == std::errc::result_out_of_range) {
      // The standard leaves the value untouched on overflow/underflow.
      CHECK(std::isinf(ours) || ours == 0.0);
    } else if (!same_bits(ours, ref)) {
      CHECK_EQ(StringView(text), "<parse mismatch>"_sv);
    }
    float fo = 0;
    float fr = 0;
    CHECK(num::parse_float(StringView(text), fo));
    auto res = std::from_chars(text.data(), text.data() + text.size(), fr);
    if (res.ec == std::errc() && std::memcmp(&fo, &fr, sizeof(float)) != 0) {
      CHECK_EQ(StringView(text), "<float parse mismatch>"_sv);
    }
  }
}

YASL_TEST(num_float_parse_hard_cases) {
  double v = 0;
  // Exactly halfway between two doubles, needing many digits to decide.
  CHECK(num::parse_double("9007199254740993"_sv, v) && v == 9007199254740992.0);
  CHECK(num::parse_double("9007199254740993.0000000000000000000001"_sv, v) &&
        v == 9007199254740994.0);
  CHECK(num::parse_double("2.2250738585072011e-308"_sv, v) &&
        same_bits(v, 2.2250738585072011e-308));
  CHECK(num::parse_double("4.9406564584124654e-324"_sv, v) && v == 5e-324);
  CHECK(num::parse_double("2.4703282292062327e-324"_sv, v) && v == 0.0);
  CHECK(num::parse_double("2.4703282292062328e-324"_sv, v) && v == 5e-324);
  CHECK(num::parse_double("1e400"_sv, v) && std::isinf(v));
  CHECK(num::parse_double("-1e-400"_sv, v) && v == 0.0 && std::signbit(v));
  CHECK(num::parse_double("Infinity"_sv, v) && std::isinf(v));
  CHECK(num::parse_double("-nan"_sv, v) && std::isnan(v));

  std::string long_one = "0." + std::string(1000, '0') + "1e1001";
  CHECK(num::parse_double(StringView(long_one), v) && v == 1.0);
  std::string many = "1" + std::string(900, '0') + "e-900";
  CHECK(num::parse_double(StringView(many), v) && v == 1.0);
}
