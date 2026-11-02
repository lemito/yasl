#include <yasl/mem/frame.h>
#include <yasl/mem/tracking.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

YASL_TEST(frame_scope_allocation_no_leak) {
  {
    Frame<4096> f;
    Vector<int> v;  // uses the frame's default resource
    for (int i = 0; i < 100; ++i) {
      v.push_back(i);
    }
    CHECK_EQ(v.size(), size_t(100));
    CHECK_EQ(v[99], 99);
  }  // Frame destroyed: all inline memory reclaimed
}

YASL_TEST(frame_overflows_to_upstream_and_frees) {
  Tracking up;
  {
    Frame<64> f(&up);
    size_t chunks_before = up.stats().live();
    Vector<int> v;  // inline buffer too small: goes upstream
    for (int i = 0; i < 1000; ++i) {
      v.push_back(i);
    }
    CHECK(up.stats().live() > chunks_before);
    CHECK_EQ(v.size(), size_t(1000));
  }
  // Everything allocated in the frame is freed when it dies.
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(frame_explicit_resource) {
  Tracking up;
  {
    Frame<4096> f(&up);
    Resource* r = f.resource();
    void* p = r->allocate(100, 8);
    CHECK(p != nullptr);
    r->deallocate(p, 100, 8);
  }
}

YASL_TEST(frame_used_tracks_stack_bytes) {
  Frame<4096> f;
  CHECK_EQ(f.used(), size_t(0));
  {
    Vector<int> v;
    v.push_back(1);
    v.push_back(2);
    CHECK(f.used() > size_t(0));
  }
}

YASL_TEST(frame_nesting) {
  {
    Frame<4096> outer;
    Vector<int> a;
    a.push_back(1);
    {
      Frame<4096> inner;
      Vector<int> b;
      b.push_back(2);
      CHECK_EQ(b.size(), size_t(1));
    }
    CHECK_EQ(a.size(), size_t(1));
    CHECK_EQ(a[0], 1);
  }
}

YASL_TEST(frame_string_allocation) {
  Frame<4096> f;
  String s = "hello world";
  CHECK_EQ(s, "hello world");
  String t = s + String("!");
  CHECK_EQ(t, "hello world!");
}
