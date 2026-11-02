#include <yasl/core/math.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(math_edge_checked_zero) {
  CHECK_EQ(*checked_add<int>(0, 0), 0);
  CHECK_EQ(*checked_sub<int>(0, 0), 0);
  CHECK_EQ(*checked_mul<int>(0, max_value<int>()), 0);
  CHECK_EQ(*checked_mul<int>(max_value<int>(), 0), 0);
}

YASL_TEST(math_edge_checked_uint16_int16) {
  CHECK(!checked_add<uint16>(uint16(60000), uint16(10000)));
  CHECK_EQ(*checked_add<uint16>(uint16(60000), uint16(5000)), uint16(65000));
  CHECK(!checked_sub<int16>(min_value<int16>(), int16(1)));
  CHECK(!checked_mul<int16>(int16(300), int16(200)));
}

YASL_TEST(math_edge_checked_boundary_values) {
  CHECK_EQ(*checked_add<int>(max_value<int>() - 1, 1), max_value<int>());
  CHECK(!checked_add<int>(max_value<int>(), 1));
  CHECK_EQ(*checked_sub<int>(min_value<int>() + 1, 1), min_value<int>());
  CHECK(!checked_sub<int>(min_value<int>(), 1));
  CHECK_EQ(*checked_mul<int>(1, max_value<int>()), max_value<int>());
}

YASL_TEST(math_edge_saturating_with_zero) {
  CHECK_EQ(saturating_add<int>(0, 0), 0);
  CHECK_EQ(saturating_sub<int>(0, 0), 0);
  CHECK_EQ(saturating_mul<int>(0, max_value<int>()), 0);
  CHECK_EQ(saturating_mul<int>(min_value<int>(), 0), 0);
}

YASL_TEST(math_edge_saturating_both_negative_mul) {
  CHECK_EQ(saturating_mul<int>(-(1 << 20), -(1 << 20)), max_value<int>());
  CHECK_EQ(saturating_mul<int8>(int8(-100), int8(-100)), max_value<int8>());
}

YASL_TEST(math_edge_wrapping_zero) {
  CHECK_EQ(wrapping_add<int>(0, 0), 0);
  CHECK_EQ(wrapping_sub<int>(0, 0), 0);
  CHECK_EQ(wrapping_mul<int>(0, 42), 0);
  CHECK_EQ(wrapping_sub<uint8>(uint8(0), uint8(0)), uint8(0));
}

YASL_TEST(math_edge_wrapping_min_add_minus_one) {
  CHECK_EQ(wrapping_add<int>(min_value<int>(), -1), max_value<int>());
  CHECK_EQ(wrapping_sub<int>(min_value<int>(), 1), max_value<int>());
}

YASL_TEST(math_edge_narrow_identity_and_boundary) {
  CHECK_EQ(*narrow<int>(42), 42);
  CHECK_EQ(*narrow<uint8>(uint8(0)), uint8(0));
  CHECK_EQ(*narrow<uint8>(uint8(255)), uint8(255));
  CHECK(!narrow<uint8>(uint8(255) + 1));
  CHECK_EQ(*narrow<int8>(int8(-128)), int8(-128));
  CHECK_EQ(*narrow<int8>(int8(127)), int8(127));
  CHECK(!narrow<int8>(int8(127) + 1));
}

YASL_TEST(math_edge_div_ceil_edge) {
  CHECK_EQ(div_ceil(uint32(0), uint32(5)), uint32(0));
  CHECK_EQ(div_ceil(uint32(1), uint32(1)), uint32(1));
  CHECK_EQ(div_ceil(uint32(10), uint32(1)), uint32(10));
  CHECK_EQ(div_ceil(uint64(7), uint64(3)), uint64(3));
  CHECK_EQ(div_ceil(uint64(6), uint64(3)), uint64(2));
  CHECK_EQ(div_ceil(uint64(5), uint64(3)), uint64(2));
}

YASL_TEST(math_edge_bit_functions_zero) {
  CHECK_EQ(bit_width(uint64(0)), 0);
  CHECK_EQ(popcount(uint64(0)), 0);
  CHECK_EQ(countr_zero(uint64(0)), 64);
  CHECK(!is_pow2(uint64(0)));
  CHECK_EQ(bit_ceil(uint64(0)), uint64(1));
  CHECK_EQ(bit_ceil(uint64(1)), uint64(1));
}

YASL_TEST(math_edge_bit_functions_powers_of_two) {
  for (int i = 0; i < 63; ++i) {
    uint64 v = uint64(1) << i;
    CHECK(is_pow2(v));
    CHECK_EQ(countr_zero(v), i);
    CHECK_EQ(popcount(v), 1);
    CHECK_EQ(bit_width(v), i + 1);
    CHECK_EQ(bit_ceil(v), v);
    if (i > 1) {
      CHECK_EQ(bit_ceil(v - 1), v);
    }
  }
}

YASL_TEST(math_edge_bit_functions_allones) {
  uint64 all = ~uint64(0);
  CHECK(!is_pow2(all));
  CHECK_EQ(popcount(all), 64);
  CHECK_EQ(countr_zero(all), 0);
  CHECK_EQ(bit_width(all), 64);
}

YASL_TEST(math_edge_countl_zero) {
  CHECK_EQ(countl_zero(uint64(0)), 64);
  CHECK_EQ(countl_zero(uint64(1)), 63);
  CHECK_EQ(countl_zero(uint64(1) << 63), 0);
  CHECK_EQ(countl_zero(~uint64(0)), 0);
}

YASL_TEST(math_edge_saturating_uint8_boundaries) {
  CHECK_EQ(saturating_add<uint8>(uint8(255), uint8(1)), uint8(255));
  CHECK_EQ(saturating_add<uint8>(uint8(255), uint8(255)), uint8(255));
  CHECK_EQ(saturating_sub<uint8>(uint8(0), uint8(255)), uint8(0));
  CHECK_EQ(saturating_mul<uint8>(uint8(255), uint8(2)), uint8(255));
}
