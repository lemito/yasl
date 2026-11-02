#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>
#include <yasl/vocab/result.h>

using namespace yasl;

YASL_TEST(result_edge_value_or_on_ok) {
  Result<int, int> r = ok(7);
  CHECK_EQ(r.value_or(99), 7);
}

YASL_TEST(result_edge_value_or_rvalue) {
  Result<String, int> r = ok(String("hello"));
  String s = yasl::move(r).value_or(String("fallback"));
  CHECK_EQ(s, "hello");

  Result<String, int> e = err(1);
  String s2 = yasl::move(e).value_or(String("fallback"));
  CHECK_EQ(s2, "fallback");
}

YASL_TEST(result_edge_operator_star_rvalue) {
  Result<String, int> r = ok(String("hello"));
  String s = *yasl::move(r);
  CHECK_EQ(s, "hello");
}

YASL_TEST(result_edge_value_rvalue) {
  Result<String, int> r = ok(String("world"));
  String s = yasl::move(r).value();
  CHECK_EQ(s, "world");
}

YASL_TEST(result_edge_error_rvalue) {
  Result<int, String> r = err(String("oops"));
  String e = yasl::move(r).error();
  CHECK_EQ(e, "oops");
}

YASL_TEST(result_edge_self_copy_assign) {
  Result<int, int> r = ok(5);
  Result<int, int>& ref = r;
  r = ref;
  CHECK(r.is_ok());
  CHECK_EQ(*r, 5);
}

YASL_TEST(result_edge_map_on_error_passes_through) {
  Result<int, int> e = err(99);
  auto mapped = yasl::move(e).map([](int v) { return v * 2; });
  CHECK(mapped.is_err());
  CHECK_EQ(mapped.error(), 99);
}

YASL_TEST(result_edge_map_err_on_ok_passes_through) {
  Result<int, int> r = ok(7);
  auto mapped = yasl::move(r).map_err([](int e) { return e * 10; });
  CHECK(mapped.is_ok());
  CHECK_EQ(*mapped, 7);
}

YASL_TEST(result_edge_and_then_on_error) {
  Result<int, int> e = err(42);
  auto result = yasl::move(e).and_then([](int v) -> Result<int, int> {
    return ok(v + 1);
  });
  CHECK(result.is_err());
  CHECK_EQ(result.error(), 42);
}

YASL_TEST(result_edge_or_else_on_ok) {
  Result<int, int> r = ok(3);
  auto result = yasl::move(r).or_else([](int) -> Result<int, int> {
    return ok(99);
  });
  CHECK(result.is_ok());
  CHECK_EQ(*result, 3);
}

YASL_TEST(result_edge_ok_method_on_error_returns_none) {
  Result<int, int> e = err(5);
  CHECK(!yasl::move(e).ok().has_value());
}

YASL_TEST(result_edge_void_copy) {
  Result<void, String> r = ok();
  auto copy = r;
  CHECK(copy.is_ok());
  Result<void, String> e = err(String("bad"));
  auto ecopy = e;
  CHECK(ecopy.is_err());
  CHECK_EQ(ecopy.error(), "bad");
}

YASL_TEST(result_edge_void_map_err) {
  Result<void, int> e = err(7);
  auto mapped = yasl::move(e).map_err([](int v) { return v * 2; });
  CHECK(mapped.is_err());
  CHECK_EQ(mapped.error(), 14);

  Result<void, int> r = ok();
  auto mapped_ok = yasl::move(r).map_err([](int v) { return v * 2; });
  CHECK(mapped_ok.is_ok());
}

YASL_TEST(result_edge_void_and_then) {
  int called = 0;
  Result<void, int> r = ok();
  auto result = yasl::move(r).and_then([&]() -> Result<void, int> {
    ++called;
    return ok();
  });
  CHECK(result.is_ok());
  CHECK_EQ(called, 1);

  Result<void, int> e = err(3);
  auto result2 = yasl::move(e).and_then([&]() -> Result<void, int> {
    ++called;
    return ok();
  });
  CHECK(result2.is_err());
  CHECK_EQ(called, 1);
}

YASL_TEST(result_edge_same_type_ok_err) {
  Result<int, int> r = ok(1);
  Result<int, int> e = err(2);
  CHECK(r.is_ok() && e.is_err());
  CHECK_EQ(*r, 1);
  CHECK_EQ(e.error(), 2);
  r = e;
  CHECK(r.is_err());
  CHECK_EQ(r.error(), 2);
}

YASL_TEST(result_edge_no_leak_on_copy_assign) {
  mem::Tracking t;
  {
    struct Tracked {
      mem::Tracking* t;
      void* p;
      explicit Tracked(mem::Tracking* t) : t(t), p(t->allocate(8)) {}
      Tracked(const Tracked& o) : t(o.t), p(t->allocate(8)) {}
      Tracked(Tracked&& o) noexcept : t(o.t), p(yasl::exchange(o.p, nullptr)) {}
      ~Tracked() {
        if (p) {
          t->deallocate(p, 8);
        }
      }
    };
    Result<Tracked, int> a = ok(Tracked(&t));
    CHECK_EQ(t.stats().live(), size_t(1));
    Result<Tracked, int> b = a;
    CHECK_EQ(t.stats().live(), size_t(2));
    b = err(1);
    CHECK_EQ(t.stats().live(), size_t(1));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(result_edge_chain_map_and_then) {
  auto r = Result<int, String>(ok(3))
               .map([](int v) { return v * 2; })
               .and_then([](int v) -> Result<String, String> {
                 return ok(String("val=") + String(v == 6 ? "6" : "?"));
               });
  CHECK(r.is_ok());
  CHECK_EQ(*r, "val=6");
}
