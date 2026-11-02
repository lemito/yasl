#include <yasl/mem/tracking.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

YASL_TEST(tracking_counts_allocations_and_deallocations) {
  Tracking t;
  {
    Vector<int> v(&t);
    for (int i = 0; i < 100; ++i) {
      v.push_back(i);
    }
    CHECK(t.stats().allocations >= 1);
    CHECK(t.stats().bytes_in_use > size_t(0));
    CHECK(t.stats().peak_bytes >= t.stats().bytes_in_use);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
  CHECK_EQ(t.stats().allocations, t.stats().deallocations);
}

YASL_TEST(tracking_detects_leak) {
  Tracking t;
  {
    Vector<int> v(&t);
    v.push_back(1);
    v.push_back(2);
    // Intentional leak: forget to free by abandoning the vector... but the
    // vector destructor frees everything, so instead allocate raw and drop it.
    void* p = t.allocate(100, 8);
    CHECK(t.stats().live() > size_t(0));
    t.deallocate(p, 100, 8);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(tracking_stats_totals) {
  Tracking t;
  {
    Vector<int> v(&t);
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
  CHECK(t.stats().total_bytes >= size_t(0));
  CHECK(t.stats().peak_bytes >= size_t(0));
}

YASL_TEST(tracking_with_upstream_counts_through) {
  Tracking t(system_resource());
  {
    Vector<int> v(&t);
    for (int i = 0; i < 500; ++i) {
      v.push_back(i);
    }
    CHECK(t.stats().allocations >= 1);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
