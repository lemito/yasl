#include <yasl/mem/arena.h>
#include <yasl/mem/resource.h>
#include <yasl/mem/sys.h>
#include <yasl/mem/tracking.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

namespace {
struct alignas(256) OverAligned {
  uint64 value = 0;
};

struct Throwing {
  explicit Throwing(int) { throw FormatError("constructor failure"); }
};
}  // namespace

YASL_TEST(resource_interface_allocate_deallocate) {
  Resource* r = system_resource();
  void* p = r->allocate(100, 16);
  CHECK(p != nullptr);
  CHECK(reinterpret_cast<uintptr_t>(p) % 16 == 0);
  r->deallocate(p, 50, 16);  // any size between requested and reported is fine
}

YASL_TEST(resource_block_reports_real_size) {
  Resource* r = system_resource();
  Block b = r->allocate_at_least(1, 8);
  CHECK(b.ptr != nullptr);
  CHECK(b.size >= 1);
  r->deallocate(b.ptr, b.size, 8);
}

YASL_TEST(resource_null_resource_throws) {
  CHECK_THROWS(null_resource()->allocate(1), BadAlloc);
  CHECK_THROWS(null_resource()->allocate_at_least(1), BadAlloc);
}

YASL_TEST(resource_default_and_set_default) {
  Resource* prev = default_resource();
  Tracking tracking;
  Resource* old = set_default_resource(&tracking);
  CHECK(old == prev);
  Resource* cur = default_resource();
  CHECK(cur == &tracking);
  set_default_resource(prev);
  CHECK(default_resource() == prev);
}

YASL_TEST(resource_scoped_default_restores) {
  Resource* prev = default_resource();
  {
    Tracking tracking;
    ScopedDefault scoped(&tracking);
    CHECK(default_resource() == &tracking);
  }
  CHECK(default_resource() == prev);
}

YASL_TEST(resource_create_and_destroy) {
  Tracking up;
  Arena arena(&up);
  int* p = arena.create<int>(42);
  CHECK_EQ(*p, 42);
  arena.destroy(p);
  CHECK_EQ(up.stats().live(), size_t(1));
  arena.release();
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(resource_create_exception_releases_allocation) {
  Tracking tracking;
  CHECK_THROWS(tracking.create<Throwing>(1), FormatError);
  CHECK_EQ(tracking.stats().live(), size_t(0));
}

YASL_TEST(resource_create_honors_overalignment) {
  Tracking tracking;
  OverAligned* p = tracking.create<OverAligned>();
  CHECK(reinterpret_cast<uintptr_t>(p) % alignof(OverAligned) == 0);
  tracking.destroy(p);
  CHECK_EQ(tracking.stats().live(), size_t(0));
}

YASL_TEST(resource_system_alignment) {
  Resource* r = system_resource();
  for (size_t align : Vector<size_t>{1, 2, 8, 16, 64, 256, 4096}) {
    void* p = r->allocate(100, align);
    CHECK(p != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(p) % align == 0);
    r->deallocate(p, 100, align);
  }
}

YASL_TEST(resource_is_equal_self) {
  Resource* r = system_resource();
  CHECK(r->is_equal(*r));
}
