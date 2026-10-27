#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>
#include <yasl/vocab/function.h>

#include <memory>

using namespace yasl;

namespace {
int apply(FunctionRef<int(int)> f, int x) { return f(x); }
}  // namespace

YASL_TEST(function_ref) {
  int k = 3;
  CHECK_EQ(apply([&](int x) { return x * k; }, 4), 12);
  auto lam = [](int x) { return x + 1; };
  FunctionRef<int(int)> r = lam;
  FunctionRef<int(int)> r2 = r;
  CHECK_EQ(r2(1), 2);
}

YASL_TEST(function_inline_and_heap) {
  mem::Tracking t;
  {
    int a = 5;
    Function<int(int)> small([a](int x) { return x + a; }, &t);
    CHECK_EQ(t.stats().live(), size_t(0));
    CHECK_EQ(small(1), 6);

    long pad[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Function<int(int)> big([pad](int x) { return x + int(pad[7]); }, &t);
    CHECK_EQ(t.stats().live(), size_t(1));
    CHECK_EQ(big(1), 9);

    Function<int(int)> moved = yasl::move(big);
    CHECK(!big);
    CHECK_EQ(moved(2), 10);
    CHECK_EQ(t.stats().live(), size_t(1));

    moved = yasl::move(small);
    CHECK_EQ(t.stats().live(), size_t(0));
    CHECK_EQ(moved(0), 5);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(function_move_only_capture) {
  mem::Tracking t;
  {
    auto p = std::make_unique<int>(41);
    Function<int()> f([p = std::move(p)]() mutable { return ++*p; }, &t);
    CHECK_EQ(f(), 42);
    CHECK_EQ(f(), 43);
    Function<int()> g = yasl::move(f);
    CHECK_EQ(g(), 44);
    Function<int()> empty;
    CHECK_THROWS(empty(), BadAccess);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(function_void_and_args) {
  int sum = 0;
  Function<void(int, int)> f([&](int a, int b) { sum = a + b; });
  f(2, 3);
  CHECK_EQ(sum, 5);
}
