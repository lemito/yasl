#include <yasl/mem/compose.h>
#include <yasl/mem/sys.h>
#include <yasl/mem/tracking.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

YASL_TEST(compose_fallback_primary_first) {
  Fallback<StackBuffer<64>, SysBlock> fb;
  // Small allocation comes from the stack buffer.
  Block b = fb.alloc(16, 8);
  CHECK(b.ptr != nullptr);
  CHECK(fb.primary().owns(b.ptr));
  CHECK_EQ(fb.primary().used(), size_t(16));
  fb.free(b.ptr, 16, 8);
  CHECK_EQ(fb.primary().used(), size_t(0));

  // Larger than the stack: falls back to the system allocator.
  Block big = fb.alloc(1000, 8);
  CHECK(big.ptr != nullptr);
  CHECK(!fb.primary().owns(big.ptr));
  fb.free(big.ptr, 1000, 8);
}

YASL_TEST(compose_fallback_as_resource_throws_on_exhaustion) {
  struct NullBlock {
    Block alloc(size_t, size_t) noexcept { return {}; }
    void free(void*, size_t, size_t) noexcept {}
    Block resize(void*, size_t, size_t, size_t) noexcept { return {}; }
    bool owns(const void*) const noexcept { return false; }
  };
  // null_resource as secondary means the fallback cannot grow.
  AsResource<Fallback<StackBuffer<16>, NullBlock>> r;
  // Fits inline.
  void* p = r.allocate(8, 8);
  CHECK(p != nullptr);
  // Overflow: secondary (null) throws BadAlloc.
  CHECK_THROWS(r.allocate(1000, 8), BadAlloc);
  r.deallocate(p, 8, 8);
}

YASL_TEST(compose_segregator_routes_by_size) {
  Tracking up;
  Segregator<32, StackBuffer<64>, SysBlock> seg;
  Block small = seg.alloc(16, 8);
  CHECK(small.ptr != nullptr);
  Block large = seg.alloc(1000, 8);
  CHECK(large.ptr != nullptr);
  seg.free(small.ptr, 16, 8);
  seg.free(large.ptr, 1000, 8);
}

YASL_TEST(compose_stack_buffer_bump_and_reset) {
  StackBuffer<128> sb;
  Block a = sb.alloc(32, 8);
  Block b = sb.alloc(32, 8);
  CHECK(a.ptr != nullptr);
  CHECK(b.ptr != nullptr);
  CHECK_EQ(sb.used(), size_t(64));
  sb.free(b.ptr, 32, 8);  // newest block rolls back
  CHECK_EQ(sb.used(), size_t(32));
  sb.reset();
  CHECK_EQ(sb.used(), size_t(0));
}

YASL_TEST(compose_vector_with_composed_resource) {
  Tracking up;
  {
    AsResource<Fallback<StackBuffer<256>, SysBlock>> r;
    Vector<int> v(&r);
    for (int i = 0; i < 1000; ++i) {
      v.push_back(i);
    }
    CHECK_EQ(v.size(), size_t(1000));
    CHECK_EQ(v[999], 999);
  }
}

YASL_TEST(compose_align_split_routes_by_alignment) {
  Tracking up;
  AlignSplit<16, StackBuffer<64>, SysBlock> as;
  Block low = as.alloc(32, 8);
  CHECK(low.ptr != nullptr);
  Block high = as.alloc(32, 64);
  CHECK(high.ptr != nullptr);
  as.free(low.ptr, 32, 8);
  as.free(high.ptr, 32, 64);
}
