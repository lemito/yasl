#include <yasl/str/builder.h>
#include <yasl/test/test.h>
#include <yasl/time/time.h>

using namespace yasl;
using namespace yasl::literals;

namespace {
String str(Duration d) {
  StringBuilder b;
  b << d;
  return b.take();
}
}  // namespace

YASL_TEST(duration_arithmetic) {
  CHECK_EQ((250_ms + 2_s).as_millis(), int64(2250));
  CHECK_EQ((1_min).as_secs(), int64(60));
  CHECK_EQ((2_h).as_secs(), int64(7200));
  CHECK_EQ((3_us).as_nanos(), int64(3000));
  CHECK_EQ((2_s * 3).as_secs(), int64(6));
  CHECK_EQ((6_s / 4).as_millis(), int64(1500));
  CHECK_EQ(6_s / 2_s, int64(3));
  CHECK(1_s > 999_ms);
  CHECK(1_s == 1000_ms);
  CHECK_EQ((-(1_s)).as_millis(), int64(-1000));
  CHECK_EQ(Duration::from_secs_f64(1.5).as_millis(), int64(1500));
}

YASL_TEST(duration_format_go_style) {
  CHECK_EQ(str(0_ns), "0s");
  CHECK_EQ(str(100_ns), "100ns");
  CHECK_EQ(str(1500_ns), "1.5us");
  CHECK_EQ(str(250_ms), "250ms");
  CHECK_EQ(str(1500_us), "1.5ms");
  CHECK_EQ(str(1_s), "1s");
  CHECK_EQ(str(1500_ms), "1.5s");
  CHECK_EQ(str(90_s), "1m30s");
  CHECK_EQ(str(60_min), "1h0m0s");
  CHECK_EQ(str(Duration::hours(72) + 3_min + 500_ms), "72h3m0.5s");
  CHECK_EQ(str(-(2_s)), "-2s");
  CHECK_EQ(str(Duration::nanos(1)), "1ns");
  CHECK_EQ(str(Duration::nanos(1'000'000'001)), "1.000000001s");
  CHECK_EQ(format("took {}", 12_ms), "took 12ms");
}

YASL_TEST(instant_and_system_time) {
  Instant a = Instant::now();
  Instant b = Instant::now();
  CHECK(b >= a);
  CHECK((b - a) >= 0_ns);
  CHECK((a + 1_s) - a == 1_s);
  CHECK(a.elapsed() >= 0_ns);
  SystemTime t = SystemTime::now();
  CHECK(t.unix_secs() > 1'600'000'000);
  CHECK((t + 5_s) - t == 5_s);
}
