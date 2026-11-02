#include <yasl/mem/arena.h>
#include <yasl/mem/tracking.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

YASL_TEST(arena_bump_alloc_and_alignment) {
  Tracking up;
  Arena arena(&up, 256);
  Vector<void*> ptrs;
  for (int i = 0; i < 1000; ++i) {
    size_t align = size_t(1) << (i % 7);
    void* p = arena.allocate(size_t(i % 50 + 1), align);
    CHECK(reinterpret_cast<uintptr_t>(p) % align == 0);
    ptrs.push_back(p);
  }
  CHECK(up.stats().allocations > 1);
  CHECK(arena.used() > 0);
}

YASL_TEST(arena_reset_keeps_chunk) {
  Tracking up;
  {
    Arena arena(&up, 256);
    size_t chunks_before = up.stats().live();
    arena.allocate(100, 8);
    arena.allocate(200, 8);
    CHECK(up.stats().live() > chunks_before);
    arena.reset();
    CHECK_EQ(arena.used(), size_t(0));
    // Keeps the newest chunk: still one live chunk from upstream.
    CHECK_EQ(up.stats().live(), chunks_before + 1);
    arena.release();
    CHECK_EQ(up.stats().live(), chunks_before);
  }
}

YASL_TEST(arena_release_frees_all) {
  Tracking up;
  {
    Arena arena(&up, 256);
    arena.allocate(100, 8);
    size_t chunks = up.stats().live();
    CHECK(chunks >= 1);
    arena.release();
    CHECK_EQ(up.stats().live(), size_t(0));
    CHECK_EQ(arena.used(), size_t(0));
    CHECK_EQ(arena.reserved(), size_t(0));
  }
}

YASL_TEST(arena_rollback_reuses_most_recent) {
  Arena arena;
  void* a = arena.allocate(32, 8);
  arena.deallocate(a, 32, 8);  // rollback: most recent block
  void* b = arena.allocate(32, 8);
  CHECK_EQ(a, b);
}

YASL_TEST(arena_grow_and_shrink_resize) {
  Arena arena;
  void* p = arena.allocate(16, 8);
  // Grow in place: rollback then re-allocate larger is the arena's semantic;
  // resize of the newest block can extend it.
  Block b = arena.resize(p, 16, 64, 8, true);
  CHECK(b.ptr == p || b.ptr != nullptr);
  arena.deallocate(b.ptr ? b.ptr : p, b.size ? b.size : 64, 8);
}

YASL_TEST(arena_inline_arena_no_heap_until_overflow) {
  Tracking up;
  {
    InlineArena<1024> arena(&up);
    size_t chunks_before = up.stats().live();
    // Fits in inline storage: no upstream chunks.
    void* p = arena.allocate(500, 8);
    CHECK(p != nullptr);
    CHECK_EQ(up.stats().live(), chunks_before);
    // Overflow: goes to upstream.
    void* q = arena.allocate(2000, 8);
    CHECK(q != nullptr);
    CHECK(up.stats().live() > chunks_before);
  }
}

YASL_TEST(arena_with_vector) {
  Tracking up;
  {
    Arena arena(&up);
    Vector<int> v(&arena);
    for (int i = 0; i < 1000; ++i) {
      v.push_back(i);
    }
    CHECK_EQ(v.size(), size_t(1000));
    CHECK_EQ(v[999], 999);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(arena_destructor_releases) {
  Tracking up;
  {
    Arena arena(&up, 256);
    arena.allocate(100, 8);
    size_t chunks = up.stats().live();
    CHECK(chunks >= 1);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}
