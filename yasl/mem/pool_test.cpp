#include <yasl/mem/pool.h>
#include <yasl/mem/tracking.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

YASL_TEST(pool_allocate_and_free_recycles) {
  Tracking up;
  {
    Pool pool(&up);
    void* a = pool.allocate(16, 8);
    void* b = pool.allocate(16, 8);
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    pool.deallocate(a, 16, 8);
    // The freed block is recycled: next same-size allocation reuses it.
    void* c = pool.allocate(16, 8);
    CHECK(c == a);
    pool.deallocate(b, 16, 8);
    pool.deallocate(c, 16, 8);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(pool_many_sizes) {
  Tracking up;
  {
    Pool pool(&up);
    Vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) {
      size_t n = size_t(8 * (i % 16 + 1));
      void* p = pool.allocate(n, 8);
      CHECK(p != nullptr);
      ptrs.push_back(p);
    }
    for (void* p : ptrs) {
      pool.deallocate(p, 64, 8);  // size between requested and reported is fine
    }
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(pool_alignment) {
  Tracking up;
  {
    Pool pool(&up);
    for (size_t align : Vector<size_t>{8, 16, 32, 64}) {
      void* p = pool.allocate(32, align);
      CHECK(p != nullptr);
      CHECK(reinterpret_cast<uintptr_t>(p) % align == 0);
      pool.deallocate(p, 32, align);
    }
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(pool_over_max_block_goes_upstream) {
  Tracking up;
  {
    Pool pool(&up, Pool::Options{.max_block = 64, .slab_size = 4096});
    void* big = pool.allocate(1000, 8);  // over max_block: upstream directly
    CHECK(big != nullptr);
    pool.deallocate(big, 1000, 8);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(pool_release_frees_slabs) {
  Tracking up;
  {
    Pool pool(&up);
    Vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) {
      ptrs.push_back(pool.allocate(32, 8));
    }
    size_t chunks = up.stats().live();
    CHECK(chunks >= 1);
    for (void* p : ptrs) {
      pool.deallocate(p, 32, 8);
    }
    // Slabs are still held until release().
    CHECK_EQ(up.stats().live(), chunks);
    pool.release();
    CHECK_EQ(up.stats().live(), size_t(0));
  }
}

YASL_TEST(pool_destructor_releases) {
  Tracking up;
  {
    Pool pool(&up);
    void* p = pool.allocate(32, 8);
    CHECK(p != nullptr);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}
