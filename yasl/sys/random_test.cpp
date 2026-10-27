#include <yasl/sys/random.h>
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
