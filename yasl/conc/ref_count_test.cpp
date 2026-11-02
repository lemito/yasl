#include <yasl/conc/atomic.h>
#include <yasl/conc/ref_count.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(ref_count_basic) {
  RefCount rc;
  CHECK_EQ(rc.count(), size_t(1));
  CHECK(rc.unique());
  rc.ref();
  CHECK_EQ(rc.count(), size_t(2));
  CHECK(!rc.unique());
  CHECK(!rc.unref());
  CHECK_EQ(rc.count(), size_t(1));
  CHECK(rc.unique());
  CHECK(rc.unref());  // last ref: caller must destroy
}

YASL_TEST(ref_count_try_ref_on_zero_fails) {
  RefCount rc{0};
  CHECK(!rc.try_ref());
  CHECK_EQ(rc.count(), size_t(0));
  rc.ref();
  CHECK(rc.try_ref());
  CHECK_EQ(rc.count(), size_t(2));
  rc.unref();
  rc.unref();
}

YASL_TEST(ref_count_release_returns_true_on_last) {
  RefCount rc{2};
  CHECK(!rc.release());  // 2 -> 1
  CHECK_EQ(rc.count(), size_t(1));
  CHECK(rc.release());  // 1 -> 0
  CHECK_EQ(rc.count(), size_t(0));
}

YASL_TEST(ref_count_thread_safety) {
  mem::Tracking t;
  {
    RefCount rc;
    Atomic<int> done{0};
    constexpr int kThreads = 8;
    constexpr int kRefs = 1000;

    Vector<Thread> threads;
    for (int i = 0; i < kThreads; ++i) {
      threads.push_back(Thread([&] {
        for (int k = 0; k < kRefs; ++k) {
          rc.ref();
        }
        for (int k = 0; k < kRefs; ++k) {
          rc.unref();
        }
        done.fetch_add(1);
      }));
    }
    for (auto& th : threads) {
      th.join();
    }
    // Balanced refs/unrefs: back to the initial single reference.
    CHECK_EQ(rc.count(), size_t(1));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(ref_count_weak_try_ref_race) {
  // try_ref must not resurrect a zero count.
  RefCount rc{1};
  CHECK(rc.unref());  // count hits 0
  CHECK(!rc.try_ref());
  CHECK(!rc.try_ref());
  CHECK_EQ(rc.count(), size_t(0));
}

YASL_TEST(ref_count_try_ref_concurrent_with_release) {
  mem::Tracking t;
  {
    RefCount rc{1};  // the "owner" reference keeps the object alive
    Atomic<int> acquired{0};
    Vector<Thread> threads;
    for (int i = 0; i < 4; ++i) {
      threads.push_back(Thread([&] {
        for (int k = 0; k < 1000; ++k) {
          if (rc.try_ref()) {
            acquired.fetch_add(1);
            rc.release();
          }
        }
      }));
    }
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(rc.count(), size_t(1));
    CHECK(acquired.load() > 0);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
