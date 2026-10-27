#include <yasl/io/fmt.h>
#include <yasl/str/builder.h>
#include <yasl/test/test.h>

using namespace yasl;

namespace {
template <class W>
String stream(const W& w) {
  StringBuilder b;
  b << w;
  return b.take();
}
}  // namespace

YASL_TEST(fmt_matches_format_integers) {
  CHECK_EQ(stream(fmt::hex(255)), format("{:x}", 255));
  CHECK_EQ(stream(fmt::HEX(48879u)), format("{:X}", 48879u));
  CHECK_EQ(stream(fmt::oct(64)), format("{:o}", 64));
  CHECK_EQ(stream(fmt::bin(5)), format("{:b}", 5));
  CHECK_EQ(stream(fmt::hex(255).alt()), format("{:#x}", 255));
  CHECK_EQ(stream(fmt::hex(255).width(8).zero_pad()), format("{:08x}", 255));
  CHECK_EQ(stream(fmt::num(42).width(6)), format("{:6}", 42));
  CHECK_EQ(stream(fmt::num(42).width(6).left()), format("{:<6}", 42));
  CHECK_EQ(stream(fmt::num(42).width(7).center().fill('*')),
           format("{:*^7}", 42));
  CHECK_EQ(stream(fmt::num(42).plus()), format("{:+}", 42));
  CHECK_EQ(stream(fmt::num(-42).width(6).zero_pad()), format("{:06}", -42));
  CHECK_EQ(stream(fmt::pad(7, 4, '.')), format("{:.>4}", 7));
}

YASL_TEST(fmt_matches_format_floats) {
  CHECK_EQ(stream(fmt::fixed(3.14159, 3)), format("{:.3f}", 3.14159));
  CHECK_EQ(stream(fmt::fixed(2.5)), format("{:f}", 2.5));
  CHECK_EQ(stream(fmt::sci(12345.678, 2)), format("{:.2e}", 12345.678));
  CHECK_EQ(stream(fmt::fixed(-1.5, 1).width(8)), format("{:8.1f}", -1.5));
  CHECK_EQ(stream(fmt::fixed(1.5f, 2).plus().zero_pad().width(9)),
           format("{:+09.2f}", 1.5f));
  CHECK_EQ(stream(fmt::num(0.1).precision(3)), format("{:.3}", 0.1));
}

YASL_TEST(fmt_inside_format_string) {
  CHECK_EQ(format("[{}]", fmt::hex(255).width(4).zero_pad()), "[00ff]");
  CHECK_EQ(format("[{:>6}]", fmt::fixed(1.5, 1)), "[   1.5]");
  CHECK_EQ(format("[{}] [{}]", fmt::grouped(1234567), fmt::percent(0.5)),
           "[1_234_567] [50%]");
  CHECK_EQ(format("[{:>8}]", fmt::bytes(2048)), "[   2 KiB]");
}

YASL_TEST(fmt_grouped) {
  CHECK_EQ(stream(fmt::grouped(0)), "0");
  CHECK_EQ(stream(fmt::grouped(999)), "999");
  CHECK_EQ(stream(fmt::grouped(1000)), "1_000");
  CHECK_EQ(stream(fmt::grouped(1000000)), "1_000_000");
  CHECK_EQ(stream(fmt::grouped(-1234567, ',')), "-1,234,567");
  CHECK_EQ(stream(fmt::grouped(uint64(18446744073709551615ull))),
           "18_446_744_073_709_551_615");
  CHECK_EQ(stream(fmt::grouped(int64(-9223372036854775807ll - 1))),
           "-9_223_372_036_854_775_808");
  CHECK_EQ(stream(fmt::grouped(12345).width(10)), "    12_345");
}

YASL_TEST(fmt_bytes_percent) {
  CHECK_EQ(stream(fmt::bytes(0)), "0 B");
  CHECK_EQ(stream(fmt::bytes(1023)), "1023 B");
  CHECK_EQ(stream(fmt::bytes(1024)), "1 KiB");
  CHECK_EQ(stream(fmt::bytes(1536)), "1.5 KiB");
  CHECK_EQ(stream(fmt::bytes(1024 * 1024)), "1 MiB");
  CHECK_EQ(stream(fmt::bytes(uint64(5) << 30)), "5 GiB");
  CHECK_EQ(stream(fmt::bytes(1024 * 1024 - 1)), "1 MiB");
  CHECK_EQ(stream(fmt::bytes(~uint64(0))), "16 EiB");
  CHECK_EQ(stream(fmt::percent(0.256, 1)), "25.6%");
  CHECK_EQ(stream(fmt::percent(1.0)), "100%");
}
