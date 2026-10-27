#include <yasl/cont/seq/small_vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <random>
#include <vector>

using namespace yasl;

YASL_TEST(small_vector_inline_then_heap) {
  mem::Tracking t;
  {
    SmallVector<int, 4> v(&t);
    for (int i = 0; i < 4; ++i) {
      v.push_back(i);
    }
    CHECK(v.is_inline());
    CHECK_EQ(t.stats().allocations, size_t(0));
    CHECK_EQ(v.capacity(), size_t(4));
    v.push_back(4);
    CHECK(!v.is_inline());
    CHECK_EQ(t.stats().live(), size_t(1));
    for (int i = 0; i < 5; ++i) {
      CHECK_EQ(v[size_t(i)], i);
    }
    v.clear_and_shrink();
    CHECK(v.is_inline() && v.empty());
    CHECK_EQ(t.stats().live(), size_t(0));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(small_vector_random_vs_std) {
  mem::Tracking t;
  {
    SmallVector<String, 6> v(&t);
    std::vector<std::string> ref;
    std::mt19937 rng(9);
    for (int i = 0; i < 20000; ++i) {
      std::string s = "string number " + std::to_string(i) +
                      " that is long enough to allocate";
      switch (rng() % 6) {
        case 0:
        case 1:
          v.push_back(String(s.c_str(), &t));
          ref.push_back(s);
          break;
        case 2:
          if (!ref.empty()) {
            v.pop_back();
            ref.pop_back();
          }
          break;
        case 3: {
          size_t at = ref.empty() ? 0 : rng() % (ref.size() + 1);
          v.insert(v.begin() + at, String(s.c_str(), &t));
          ref.insert(ref.begin() + long(at), s);
          break;
        }
        case 4:
          if (!ref.empty()) {
            size_t at = rng() % ref.size();
            v.erase(v.begin() + at);
            ref.erase(ref.begin() + long(at));
          }
          break;
        default:
          if (ref.size() > 30) {
            v.retain([](const String& x) { return x.size() % 2 == 0; });
            std::vector<std::string> kept;
            for (auto& r : ref) {
              if (r.size() % 2 == 0) {
                kept.push_back(r);
              }
            }
            ref = kept;
          }
      }
      CHECK_EQ(v.size(), ref.size());
    }
    for (size_t i = 0; i < ref.size(); ++i) {
      CHECK(v[i] == ref[i].c_str());
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(small_vector_copy_move_swap) {
  mem::Tracking t;
  {
    SmallVector<String, 3> a({String("x1", &t), String("x2", &t)}, &t);
    SmallVector<String, 3> b = a;
    CHECK(a == b);
    SmallVector<String, 3> c = yasl::move(a);
    CHECK(a.empty());
    CHECK_EQ(c.size(), size_t(2));
    for (int i = 0; i < 10; ++i) {
      c.push_back(
          String("a much longer element to force a real allocation", &t));
    }
    CHECK(!c.is_inline());
    SmallVector<String, 3> d = yasl::move(c);  // steals the heap buffer
    CHECK(c.is_inline() && c.empty());
    CHECK_EQ(d.size(), size_t(12));
    d.swap(b);
    CHECK_EQ(d.size(), size_t(2));
    CHECK_EQ(b.size(), size_t(12));
    b = d;
    CHECK(b == d);
    b = yasl::move(d);
    CHECK_EQ(b.size(), size_t(2));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(small_vector_move_only_and_resize) {
  mem::Tracking t;
  {
    SmallVector<Unique<int>, 2> v(&t);
    for (int i = 0; i < 20; ++i) {
      v.push_back(make_unique_in<int>(&t, i));
    }
    CHECK_EQ(*v[19], 19);
    Unique<int> last = v.pop();
    CHECK_EQ(*last, 19);
    SmallVector<int, 4> r;
    r.resize(10, 7);
    CHECK_EQ(r[9], 7);
    r.resize(2);
    CHECK_EQ(r.size(), size_t(2));
    r.push_back(r[0]);  // self reference
    CHECK_EQ(r[2], 7);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
