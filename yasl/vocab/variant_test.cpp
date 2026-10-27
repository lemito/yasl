#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>
#include <yasl/vocab/variant.h>

using namespace yasl;

YASL_TEST(variant_basic) {
  Variant<int, String, double> v;
  CHECK_EQ(v.index(), size_t(0));
  CHECK(v.is<int>());
  CHECK_EQ(*v.get<int>(), 0);
  v = String("hello");
  CHECK_EQ(v.index(), size_t(1));
  CHECK(v.get<int>() == nullptr);
  CHECK_EQ(*v.get<String>(), "hello");
  v = 2.5;
  CHECK(v.is<double>());
  v = 7;  // int
  CHECK_EQ(*v.get<int>(), 7);
  v.emplace<String>("emplaced");
  CHECK_EQ(v.get<String>()->size(), size_t(8));
  CHECK(!v.valueless());
}

YASL_TEST(variant_match_and_visit) {
  Variant<int, String> v = String("abc");
  int r = v.match([](int i) { return i; },
                  [](const String& s) { return int(s.size()); });
  CHECK_EQ(r, 3);
  v = 40;
  CHECK_EQ(v.visit([](auto&& x) { return int(sizeof(x)); }), int(sizeof(int)));
  v.match([](int& i) { i += 2; }, [](String&) {});
  CHECK_EQ(*v.get<int>(), 42);
  const auto& cv = v;
  CHECK_EQ(
      cv.match([](int i) { return i * 2; }, [](const String&) { return 0; }),
      84);
}

YASL_TEST(variant_copy_move_compare) {
  mem::Tracking t;
  {
    Variant<int, String> a =
        String("a string long enough to allocate on the heap...", &t);
    Variant<int, String> b = a;
    CHECK(a == b);
    Variant<int, String> c = yasl::move(a);
    CHECK(c == b);
    c = 5;
    CHECK(!(c == b));
    b = c;
    CHECK(b == c);
    Variant<int, String> d;
    d = yasl::move(b);
    CHECK_EQ(*d.get<int>(), 5);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(variant_move_only_and_destruction) {
  mem::Tracking t;
  {
    Variant<int, Unique<int>> v = make_unique_in<int>(&t, 3);
    CHECK_EQ(**v.get<Unique<int>>(), 3);
    Variant<int, Unique<int>> w = yasl::move(v);
    CHECK_EQ(**w.get<Unique<int>>(), 3);
    w = 1;
    CHECK_EQ(t.stats().live(), size_t(0));
  }
  static_assert(sizeof(Variant<char, double>) <= 16);
}
