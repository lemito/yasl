#include <yasl/cont/seq/vector.h>
#include <yasl/mem/obj_pool.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

using namespace yasl;

namespace {
struct Logged {
  Logged(Vector<int>* log, int id) : log_(log), id_(id) { log_->push_back(id); }
  ~Logged() { log_->push_back(-id_); }

  Vector<int>* log_;
  int id_;
};

struct Throws {
  explicit Throws(int* destroyed) : destroyed_(destroyed) {
    throw Exception("boom");
  }
  ~Throws() { ++*destroyed_; }

  int* destroyed_;
};

int g_counted = 0;

struct Counted {
  ~Counted() { ++g_counted; }
};

struct alignas(64) Wide {
  int v = 7;
  ~Wide() {}
};
}  // namespace

YASL_TEST(obj_pool_destroys_in_reverse_order) {
  Vector<int> log;
  {
    mem::ObjPool pool;
    pool.make<Logged>(&log, 1);
    pool.make<Logged>(&log, 2);
    pool.make<Logged>(&log, 3);
    CHECK_EQ(pool.tracked(), 3u);
    CHECK_EQ(log.size(), 3u);
  }
  CHECK_EQ(log.size(), 6u);
  CHECK_EQ(log[3], -3);
  CHECK_EQ(log[4], -2);
  CHECK_EQ(log[5], -1);
}

YASL_TEST(obj_pool_trivial_types_are_not_tracked) {
  mem::ObjPool pool;
  int* a = pool.make<int>(5);
  int* b = pool.make<int>(6);
  CHECK_EQ(*a, 5);
  CHECK_EQ(*b, 6);
  CHECK_EQ(pool.tracked(), 0u);
}

YASL_TEST(obj_pool_constructor_exception_registers_nothing) {
  Vector<int> log;
  int destroyed = 0;
  mem::ObjPool pool;
  pool.make<Logged>(&log, 1);
  CHECK_THROWS(pool.make<Throws>(&destroyed), Exception);
  CHECK_EQ(pool.tracked(), 1u);
  pool.make<Logged>(&log, 2);
  pool.release();
  CHECK_EQ(destroyed, 0);  // the half-built object is not destroyed
  CHECK_EQ(log.size(), 4u);
  CHECK_EQ(log[2], -2);
  CHECK_EQ(log[3], -1);
}

YASL_TEST(obj_pool_array) {
  Vector<int> log;
  mem::ObjPool pool;
  g_counted = 0;
  Counted* arr = pool.make_array<Counted>(5);
  CHECK(arr != nullptr);
  int* ints = pool.make_array<int>(4);
  for (int i = 0; i < 4; ++i) {
    CHECK_EQ(ints[i], 0);
  }
  CHECK_EQ(g_counted, 0);
  pool.reset();
  CHECK_EQ(g_counted, 5);
  CHECK_EQ(pool.tracked(), 0u);
}

YASL_TEST(obj_pool_alignment) {
  mem::ObjPool pool;
  pool.make<char>('x');
  Wide* w = pool.make<Wide>();
  CHECK_EQ(reinterpret_cast<uintptr_t>(w) % 64, 0u);
  CHECK_EQ(w->v, 7);
}

YASL_TEST(obj_pool_copy_str) {
  mem::ObjPool pool;
  String temp("hello");
  StringView v = pool.copy_str(temp.view());
  temp = "changed";
  CHECK_EQ(v, "hello");
  CHECK_EQ(v.data()[v.size()], '\0');
  CHECK_EQ(pool.copy_str("").size(), 0u);
}

YASL_TEST(obj_pool_no_leaks) {
  mem::Tracking t;
  {
    mem::ObjPool pool(&t);
    for (int i = 0; i < 1000; ++i) {
      pool.make<String>("some string that is long enough to need a heap block");
    }
    CHECK(t.stats().live() > 0);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(obj_pool_nested) {
  Vector<int> log;
  mem::Tracking t;
  {
    mem::ObjPool outer(&t);
    mem::ObjPool* inner = outer.make<mem::ObjPool>(&outer);
    inner->make<Logged>(&log, 1);
    outer.make<Logged>(&log, 2);
    inner->make<Logged>(&log, 3);
  }
  // outer dies newest first: 2, then the inner pool (which destroys 3, 1)
  CHECK_EQ(log.size(), 6u);
  CHECK_EQ(log[3], -2);
  CHECK_EQ(log[4], -3);
  CHECK_EQ(log[5], -1);
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(obj_pool_scoped_default) {
  mem::ObjPool pool;
  {
    mem::ScopedDefault use(&pool);
    Vector<int> v;
    for (int i = 0; i < 100; ++i) {
      v.push_back(i);
    }
    String s("text in the pool");
    CHECK(pool.arena().used() > 0);
    CHECK_EQ(v.resource(), &pool);
  }
  CHECK(mem::default_resource() != &pool);
}

YASL_TEST(obj_pool_as_container_resource) {
  mem::Tracking t;
  {
    mem::ObjPool pool(&t);
    Vector<String> v(&pool);
    for (int i = 0; i < 50; ++i) {
      v.push_back(String("item number ") + String("x"));
    }
    CHECK_EQ(v.size(), 50u);
  }
  CHECK_EQ(t.stats().live(), 0u);
}
