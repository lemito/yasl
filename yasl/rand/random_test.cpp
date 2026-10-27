#include <yasl/rand/random.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(random_deterministic_and_distinct) {
  Random a(42);
  Random b(42);
  Random c(43);
  bool differs = false;
  for (int i = 0; i < 100; ++i) {
    uint64 x = a.next_u64();
    CHECK_EQ(x, b.next_u64());
    differs |= x != c.next_u64();
  }
  CHECK(differs);
  CHECK(os_random_u64() != os_random_u64());
  Random r = Random::from_os();
  (void)r.next_u32();
}

YASL_TEST(random_range_is_uniform_and_bounded) {
  Random rng(7);
  int counts[6] = {};
  for (int i = 0; i < 60000; ++i) {
    int v = rng.range(1, 7);
    CHECK(v >= 1 && v < 7);
    ++counts[v - 1];
  }
  for (int c : counts) {
    CHECK(c > 9000 && c < 11000);
  }
  for (int i = 0; i < 1000; ++i) {
    int v = rng.range(-5, 5);
    CHECK(v >= -5 && v < 5);
    CHECK(rng.below(1) == 0);
  }
  CHECK_EQ(rng.range(int8(-128), int8(127)) >= -128, true);
  CHECK(rng.range(uint64(0), ~uint64(0)) <= ~uint64(0));
}

YASL_TEST(random_double_and_shuffle) {
  Random rng(1);
  double sum = 0;
  for (int i = 0; i < 100000; ++i) {
    double d = rng.next_double();
    CHECK(d >= 0.0 && d < 1.0);
    sum += d;
  }
  CHECK(sum > 49000 && sum < 51000);
  int v[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  rng.shuffle(Span<int>(v));
  int total = 0;
  bool moved = false;
  for (int i = 0; i < 10; ++i) {
    total += v[i];
    moved |= v[i] != i;
  }
  CHECK_EQ(total, 45);
  CHECK(moved);
}

YASL_TEST(pcg32_reference_values) {
  Pcg32 rng(42, 54);
  CHECK_EQ(rng.next_u32(), 0xa15c02b7u);
  CHECK_EQ(rng.next_u32(), 0x7b47f409u);
  CHECK_EQ(rng.next_u32(), 0xba1d3330u);
  CHECK_EQ(rng.next_u32(), 0x83d2f293u);
  CHECK_EQ(rng.next_u32(), 0xbfa4784bu);
  CHECK_EQ(rng.next_u32(), 0xcbed606eu);
}

YASL_TEST(pcg64_reference_values) {
  Pcg64 rng(42, 54);
  CHECK_EQ(rng.next_u64(), uint64(0x86b1da1d72062b68ull));
  CHECK_EQ(rng.next_u64(), uint64(0x1304aa46c9853d39ull));
  CHECK_EQ(rng.next_u64(), uint64(0xa3670e9e0dd50358ull));
  CHECK_EQ(rng.next_u64(), uint64(0xf9090e529a7dae00ull));
  CHECK_EQ(rng.next_u64(), uint64(0xc85b9fd837996f2cull));
  CHECK_EQ(rng.next_u64(), uint64(0x606121f8e3919196ull));
}

template <class G>
static void check_generator_api(G rng) {
  // range() stays inside its bounds and eventually hits all values
  bool seen[6] = {};
  for (int i = 0; i < 2000; ++i) {
    int v = rng.range(1, 7);
    CHECK(v >= 1 && v < 7);
    seen[v - 1] = true;
  }
  for (bool s : seen) {
    CHECK(s);
  }
  for (int i = 0; i < 1000; ++i) {
    double d = rng.next_double();
    CHECK(d >= 0.0 && d < 1.0);
    CHECK(rng.below(10) < 10);
  }
  int items[20];
  for (int i = 0; i < 20; ++i) {
    items[i] = i;
  }
  rng.shuffle(Span<int>(items, 20));
  int sum = 0;
  bool moved = false;
  for (int i = 0; i < 20; ++i) {
    sum += items[i];
    moved |= items[i] != i;
  }
  CHECK_EQ(sum, 190);
  CHECK(moved);
  (void)rng.next_bool();
  (void)rng.next_u32();
}

YASL_TEST(pcg_shares_the_random_api) {
  check_generator_api(Pcg32(1));
  check_generator_api(Pcg64(1));
  check_generator_api(Random(1));
  // Same seed, same sequence; different streams differ.
  Pcg32 a(7, 1), b(7, 1), c(7, 2);
  CHECK_EQ(a.next_u64(), b.next_u64());
  CHECK(a.next_u64() != c.next_u64());
  Pcg64 d = Pcg64::from_os();
  Pcg64 e = Pcg64::from_os();
  CHECK(d.next_u64() != e.next_u64());
}
