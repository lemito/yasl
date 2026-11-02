#include <yasl/mem/sys.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

YASL_TEST(sys_alloc_alignment_and_size) {
  for (size_t align : Vector<size_t>{1, 8, 16, 64, 256, 4096}) {
    Block b = sys::alloc(100, align);
    CHECK(b.ptr != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(b.ptr) % align == 0);
    CHECK(b.size >= 100);
    sys::free(b.ptr, b.size, align);
  }
}

YASL_TEST(sys_free_accepts_range_of_sizes) {
  Block b = sys::alloc(100, 8);
  CHECK(b.ptr != nullptr);
  // Deallocating with any size between requested and reported is accepted.
  sys::free(b.ptr, 100, 8);
  Block b2 = sys::alloc(200, 8);
  sys::free(b2.ptr, b2.size, 8);
}

YASL_TEST(sys_alloc_zero_bytes) {
  Block b = sys::alloc(0, 8);
  // A zero-size alloc may return nullptr or a non-null block; both are valid.
  if (b.ptr) {
    sys::free(b.ptr, b.size, 8);
  }
}

YASL_TEST(sys_resize_in_place_or_fail) {
  Block b = sys::alloc(16, 8);
  CHECK(b.ptr != nullptr);
  Block grown = sys::resize(b.ptr, b.size, 64, 8);
  if (grown.ptr) {
    sys::free(grown.ptr, grown.size, 8);
  } else {
    // Cannot grow in place: block untouched, caller moves.
    sys::free(b.ptr, b.size, 8);
  }
}

YASL_TEST(sys_good_size_is_at_least_requested) {
  for (size_t s : Vector<size_t>{1, 7, 8, 100, 1000, 4096}) {
    size_t gs = sys::good_size(s);
    CHECK(gs >= s);
  }
}

YASL_TEST(sys_alloc_large_block) {
  Block b = sys::alloc(1024 * 1024, 4096);
  CHECK(b.ptr != nullptr);
  CHECK(reinterpret_cast<uintptr_t>(b.ptr) % 4096 == 0);
  sys::free(b.ptr, b.size, 4096);
}
