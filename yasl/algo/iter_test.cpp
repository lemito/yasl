#include <yasl/algo/iter.h>
#include <yasl/cont/seq/deque.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <vector>

using namespace yasl;
using namespace yasl::iter;

YASL_TEST(iter_map_filter_collect) {
  Vector<int> v{1, 2, 3, 4, 5, 6};
  auto out = v | filter([](int x) { return x % 2 == 0; }) |
             map([](int x) { return x * x; }) | collect<Vector>();
  CHECK((std::vector<int>(out.begin(), out.end()) ==
         std::vector<int>{4, 16, 36}));
  auto d = v | take(2) | collect<Deque>();
  CHECK_EQ(d.size(), size_t(2));
  CHECK_EQ(d.back(), 2);
}

YASL_TEST(iter_laziness) {
  Vector<int> v{1, 2, 3, 4, 5};
  int calls = 0;
  auto chain = v | map([&](int x) {
                 ++calls;
                 return x + 1;
               });
  CHECK_EQ(calls, 0);
  auto first_two = chain | take(2) | collect<Vector>();
  CHECK_EQ(calls, 2);
  CHECK_EQ(first_two[1], 3);
}

YASL_TEST(iter_enumerate_zip_reverse_skip) {
  Vector<String> names{"a", "b", "c"};
  size_t total = 0;
  for (auto [i, s] : names | enumerate()) {
    total += i * s.size();
    CHECK(s.size() == 1);
  }
  CHECK_EQ(total, size_t(0 + 1 + 2));

  Vector<int> xs{1, 2, 3, 4};
  Vector<int> ys{10, 20, 30};
  int dot = 0;
  for (auto [x, y] : xs | zip(ys)) {
    dot += x * y;
  }
  CHECK_EQ(dot, 10 + 40 + 90);

  auto rev = xs | reverse() | collect<Vector>();
  CHECK((std::vector<int>(rev.begin(), rev.end()) ==
         std::vector<int>{4, 3, 2, 1}));
  auto rest = xs | skip(3) | collect<Vector>();
  CHECK_EQ(rest.size(), size_t(1));
  CHECK_EQ((xs | skip(10) | count()), size_t(0));

  for (auto [i, x] : xs | enumerate()) {
    x += int(i);  // mutates the source
  }
  CHECK_EQ(xs[3], 7);
}

YASL_TEST(iter_terminals) {
  CHECK_EQ((range(1, 101) | sum()), 5050);
  CHECK_EQ((range(5) | count()), size_t(5));
  CHECK_EQ((range(1, 5) | fold(1, [](int acc, int x) { return acc * x; })), 24);
  CHECK((range(10) | any([](int x) { return x == 7; })));
  CHECK(!(range(10) | any([](int x) { return x == 70; })));
  CHECK((range(1, 10) | all([](int x) { return x > 0; })));
  CHECK_EQ(*(range(10) | find([](int x) { return x * x > 20; })), 5);
  CHECK(!(range(3) | find([](int x) { return x > 5; })).has_value());
  Vector<int> v{3, 8, 1};
  int* hit = (v | find([](int x) { return x > 5; })).get();
  CHECK(hit == &v[1]);
  CHECK_EQ((range(0, 0) | sum()), 0);
  CHECK_EQ((range(5, 2) | count()), size_t(0));
  Vector<double> ds{0.5, 1.5};
  CHECK_EQ((ds | sum()), 2.0);
}
