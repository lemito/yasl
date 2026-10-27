#include <yasl/core/math.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(math_checked) {
  CHECK(!checked_add<int>(max_value<int>(), 1));
  CHECK_EQ(*checked_add<int>(1, 2), 3);
  CHECK(!checked_sub<uint32>(1u, 2u));
  CHECK_EQ(*checked_sub<int8>(int8(-100), int8(27)), int8(-127));
  CHECK(!checked_mul<int64>(max_value<int64>(), 2));
  CHECK_EQ(*checked_mul<uint8>(uint8(15), uint8(17)), uint8(255));
  CHECK(!checked_mul<uint8>(uint8(16), uint8(16)));
}

YASL_TEST(math_saturating) {
  CHECK_EQ(saturating_add<int>(max_value<int>(), 5), max_value<int>());
  CHECK_EQ(saturating_add<int>(min_value<int>(), -5), min_value<int>());
  CHECK_EQ(saturating_add<uint8>(200, 100), uint8(255));
  CHECK_EQ(saturating_sub<uint8>(3, 5), uint8(0));
  CHECK_EQ(saturating_sub<int>(min_value<int>(), 1), min_value<int>());
  CHECK_EQ(saturating_sub<int>(max_value<int>(), -1), max_value<int>());
  CHECK_EQ(saturating_mul<int>(1 << 20, 1 << 20), max_value<int>());
  CHECK_EQ(saturating_mul<int>(-(1 << 20), 1 << 20), min_value<int>());
  CHECK_EQ(saturating_mul<int>(-(1 << 20), -(1 << 20)), max_value<int>());
}

YASL_TEST(math_wrapping) {
  CHECK_EQ(wrapping_add<int>(max_value<int>(), 1), min_value<int>());
  CHECK_EQ(wrapping_sub<uint8>(0, 1), uint8(255));
  CHECK_EQ(wrapping_mul<int8>(int8(100), int8(2)), int8(-56));
}

YASL_TEST(math_narrow_and_bits) {
  CHECK_EQ(*narrow<uint8>(255), uint8(255));
  CHECK(!narrow<uint8>(256));
  CHECK(!narrow<uint8>(-1));
  CHECK(!narrow<int>(uint32(3'000'000'000u)));
  CHECK(!narrow<uint32>(int64(-1)));
  CHECK_EQ(*narrow<int8>(-128), int8(-128));
  CHECK_EQ(div_ceil<uint32>(7, 2), 4u);
  CHECK_EQ(div_ceil<uint32>(8, 2), 4u);
  CHECK(is_pow2(64u));
  CHECK(!is_pow2(0u));
  CHECK(!is_pow2(12u));
  CHECK_EQ(popcount(uint64(0xff00ff)), 16);
  CHECK_EQ(countr_zero(uint32(8)), 3);
  CHECK_EQ(countr_zero(uint32(0)), 32);
  CHECK_EQ(bit_width(uint32(255)), 8);
}
