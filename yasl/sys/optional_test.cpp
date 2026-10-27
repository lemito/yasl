#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/sys/optional.h>
#include <yasl/sys/result.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(optional_map_and_then) {
    Optional<int> a = 4;
    CHECK_EQ(*a.map([](int v) { return v * 2; }), 8);
    CHECK(!Optional<int>().map([](int v) { return v; }).has_value());
    auto b = a.and_then([](int v) -> Optional<String> {
        if (v > 3) {
            return String("big");
        }
        return none;
    });
    CHECK_EQ(*b, "big");
    CHECK(!Optional<int>(1).and_then([](int) -> Optional<int> { return none; }));
}

YASL_TEST(optional_or_else_filter_take) {
    CHECK_EQ(*Optional<int>().or_else([] { return Optional<int>(9); }), 9);
    CHECK_EQ(*Optional<int>(1).or_else([] { return Optional<int>(9); }), 1);
    CHECK_EQ(*Optional<int>(5).filter([](int v) { return v > 3; }), 5);
    CHECK(!Optional<int>(2).filter([](int v) { return v > 3; }));
    Optional<String> s = String("abc");
    auto t = s.take();
    CHECK(!s.has_value());
    CHECK_EQ(*t, "abc");
    CHECK_EQ(Optional<int>().value_or_else([] { return 7; }), 7);
}

YASL_TEST(optional_ok_or) {
    auto r = Optional<int>(3).ok_or(1);
    CHECK_EQ(*r, 3);
    auto e = Optional<int>().ok_or(5);
    CHECK_EQ(e.error(), 5);
}

YASL_TEST(optional_ref) {
    int x = 10;
    Optional<int&> r = x;
    CHECK(r);
    *r = 11;
    CHECK_EQ(x, 11);
    CHECK_EQ(*r.map([](int& v) { return v + 1; }), 12);
    Optional<int&> n;
    CHECK(!n);
    CHECK_THROWS(n.value(), BadAccess);
    CHECK_EQ(n.value_or(4), 4);
    auto t = r.take();
    CHECK(t);
    CHECK(r == none);
}

YASL_TEST(optional_no_leak) {
    mem::Tracking t;
    {
        Optional<String> o = String("a long string that is certainly not inline", &t);
        auto m = yasl::move(o).map([](String&& s) { return yasl::move(s); });
        CHECK(m.has_value());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}
