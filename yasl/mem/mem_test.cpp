#include <yasl/compat/allocator.h>
#include <yasl/compat/pmr.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/arena.h>
#include <yasl/mem/compose.h>
#include <yasl/mem/frame.h>
#include <yasl/mem/pool.h>
#include <yasl/mem/sync.h>
#include <yasl/mem/sys.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <memory_resource>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace yasl;

namespace {
bool aligned(const void* p, size_t a) {
  return reinterpret_cast<uintptr_t>(p) % a == 0;
}
}  // namespace

YASL_TEST(mem_system_alignment) {
  mem::Resource* r = mem::system_resource();
  for (size_t align : {1, 2, 8, 16, 64, 256, 4096}) {
    void* p = r->allocate(100, align);
    CHECK(aligned(p, align));
    r->deallocate(p, 100, align);
  }
}

YASL_TEST(mem_null_resource_throws) {
  CHECK_THROWS(mem::null_resource()->allocate(1), BadAlloc);
}

YASL_TEST(mem_arena_basic) {
  mem::Tracking up;
  {
    mem::Arena arena(&up, 256);
    std::vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) {
      size_t align = size_t(1) << (i % 7);
      void* p = arena.allocate(size_t(i % 50 + 1), align);
      CHECK(aligned(p, align));
      ptrs.push_back(p);
    }
    CHECK(up.stats().allocations > 1);
    CHECK(arena.used() > 0);

    size_t chunks = up.stats().live();
    arena.reset();
    CHECK_EQ(up.stats().live(), size_t(1));
    CHECK(chunks >= 1);
    CHECK_EQ(arena.used(), size_t(0));

    arena.release();
    CHECK_EQ(up.stats().live(), size_t(0));
    (void)arena.allocate(10);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(mem_arena_rollback) {
  mem::Arena arena;
  void* a = arena.allocate(32, 8);
  arena.deallocate(a, 32, 8);
  void* b = arena.allocate(32, 8);
  CHECK_EQ(a, b);
}

YASL_TEST(mem_arena_huge_request) {
  mem::Tracking up;
  mem::Arena arena(&up, 64);
  void* p = arena.allocate(1 << 20, 64);
  CHECK(aligned(p, 64));
  CHECK(arena.reserved() >= (1u << 20));
}

YASL_TEST(mem_inline_arena) {
  mem::Tracking up;
  mem::InlineArena<1024> arena(&up);
  auto* begin = reinterpret_cast<char*>(&arena);
  auto* end = begin + sizeof(arena);
  void* p = arena.allocate(100);
  CHECK(static_cast<char*>(p) >= begin && static_cast<char*>(p) < end);
  CHECK_EQ(up.stats().allocations, size_t(0));
  (void)arena.allocate(2000);
  CHECK_EQ(up.stats().allocations, size_t(1));
  arena.release();
  CHECK_EQ(up.stats().live(), size_t(0));
  void* q = arena.allocate(100);
  CHECK_EQ(p, q);
}

YASL_TEST(mem_arena_null_upstream_is_bounded) {
  alignas(16) char buf[128];
  mem::Arena arena(buf, sizeof(buf), mem::null_resource());
  (void)arena.allocate(100);
  CHECK_THROWS(arena.allocate(100), BadAlloc);
}

YASL_TEST(mem_pool_reuse_and_classes) {
  mem::Tracking up;
  {
    mem::Pool pool(&up);
    mem::Block b = pool.allocate_at_least(20, 4);
    CHECK_EQ(b.size, size_t(32));
    pool.deallocate(b.ptr, b.size, 4);
    void* again = pool.allocate(17, 8);
    CHECK_EQ(again, b.ptr);
    pool.deallocate(again, 17, 8);

    for (size_t align : {1, 2, 4, 8, 16}) {
      for (size_t size = 1; size < 3000; size = size * 3 + 1) {
        void* p = pool.allocate(size, align);
        CHECK(aligned(p, align));
        pool.deallocate(p, size, align);
      }
    }

    size_t before = up.stats().allocations;
    void* big = pool.allocate(1 << 16);
    CHECK_EQ(up.stats().allocations, before + 1);
    pool.deallocate(big, 1 << 16);

    void* over = pool.allocate(64, 128);
    CHECK(aligned(over, 128));
    pool.deallocate(over, 64, 128);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(mem_pool_many_objects) {
  mem::Tracking up;
  {
    mem::Pool pool(&up);
    std::vector<std::pair<void*, size_t>> live;
    for (size_t i = 0; i < 20000; ++i) {
      size_t size = (i * 7919) % 700 + 1;
      live.emplace_back(pool.allocate(size), size);
      std::memset(live.back().first, int(i), size);
      if (i % 3 == 0) {
        auto [p, s] = live[i / 2];
        if (p) {
          pool.deallocate(p, s);
          live[i / 2].first = nullptr;
        }
      }
    }
    for (auto [p, s] : live) {
      if (p) {
        pool.deallocate(p, s);
      }
    }
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(mem_tracking_stats) {
  mem::Tracking t;
  void* a = t.allocate(100);
  void* b = t.allocate(50);
  CHECK_EQ(t.stats().allocations, size_t(2));
  CHECK(t.stats().bytes_in_use >= 150);
  t.deallocate(a, 100);
  t.deallocate(b, 50);
  CHECK_EQ(t.stats().live(), size_t(0));
  CHECK_EQ(t.stats().bytes_in_use, size_t(0));
  CHECK(t.stats().peak_bytes >= 150);
}

YASL_TEST(mem_scoped_default) {
  mem::Tracking t;
  mem::Resource* before = mem::default_resource();
  {
    mem::ScopedDefault scope(&t);
    CHECK_EQ(mem::default_resource(), static_cast<mem::Resource*>(&t));
    Vector<int> v;
    v.push_back(1);
    CHECK_EQ(v.resource(), static_cast<mem::Resource*>(&t));
    CHECK_EQ(t.stats().live(), size_t(1));

    std::vector<int, mem::Allocator<int>> sv;
    sv.push_back(1);
    CHECK_EQ(t.stats().live(), size_t(2));
  }
  CHECK_EQ(mem::default_resource(), before);
  CHECK_EQ(t.stats().live(), size_t(0));

  std::thread([&] {
    // A new thread starts with the global default, not the scoped one.
    CHECK_EQ(mem::default_resource(), before);
  }).join();
}

YASL_TEST(mem_allocator_with_std_containers) {
  mem::Tracking t;
  {
    using Str =
        std::basic_string<char, std::char_traits<char>, mem::Allocator<char>>;
    mem::Pool pool(&t);

    std::vector<int, mem::Allocator<int>> v(&pool);
    for (int i = 0; i < 1000; ++i) {
      v.push_back(i);
    }
    CHECK_EQ(v[999], 999);

    std::map<int, Str, std::less<>, mem::Allocator<std::pair<const int, Str>>>
        m(&pool);
    for (int i = 0; i < 100; ++i) {
      m.emplace(i,
                Str("a fairly long string that does not fit into SSO", &pool));
    }
    CHECK_EQ(m.size(), size_t(100));
    CHECK_EQ(m.at(5).get_allocator().resource(),
             static_cast<mem::Resource*>(&pool));

    std::unordered_map<int, int, std::hash<int>, std::equal_to<>,
                       mem::Allocator<std::pair<const int, int>>>
        um(&pool);
    for (int i = 0; i < 1000; ++i) {
      um[i] = i * i;
    }
    CHECK_EQ(um[30], 900);

    std::list<int, mem::Allocator<int>> l(&pool);
    l.assign(10, 7);
    CHECK_EQ(l.size(), size_t(10));

    // Move-assignment propagates the allocator: no element-wise copy.
    std::vector<int, mem::Allocator<int>> other(mem::system_resource());
    other = std::move(v);
    CHECK_EQ(other.get_allocator().resource(),
             static_cast<mem::Resource*>(&pool));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(mem_resources_are_pmr) {
  mem::Tracking t;
  {
    mem::Arena arena(&t);
    mem::PmrAdapter pmr(&arena);
    std::pmr::vector<std::pmr::string> v(&pmr);
    for (int i = 0; i < 100; ++i) {
      v.emplace_back("pmr strings live in the yasl arena as well, nice");
    }
    CHECK(t.stats().allocations > 0);
    CHECK_EQ(v[0].get_allocator().resource(),
             static_cast<std::pmr::memory_resource*>(&pmr));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(mem_from_pmr_adapter) {
  std::pmr::monotonic_buffer_resource mono;
  mem::FromPmr res(&mono);
  Vector<int> v(&res);
  for (int i = 0; i < 100; ++i) {
    v.push_back(i);
  }
  CHECK_EQ(v.back(), 99);
}

YASL_TEST(mem_synchronized_pool_threads) {
  mem::Tracking t;
  {
    mem::Synchronized<mem::Pool> pool(&t);
    std::vector<std::thread> threads;
    for (int k = 0; k < 4; ++k) {
      threads.emplace_back([&pool, k] {
        Vector<Vector<int>> vv(&pool);
        for (int i = 0; i < 2000; ++i) {
          vv.emplace_back(&pool).push_back(i + k);
          if (i % 2) {
            vv.pop_back();
          }
        }
      });
    }
    for (auto& th : threads) {
      th.join();
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(sys_backend) {
  mem::Block b = mem::sys::alloc(100, mem::kMaxAlign);
  CHECK(b.ptr != nullptr);
  CHECK(b.size >= 100);
  CHECK(mem::sys::good_size(100) >= 100);
  mem::sys::free(b.ptr, 100, mem::kMaxAlign);

  for (size_t align : {size_t(64), size_t(4096)}) {
    mem::Block a = mem::sys::alloc(300, align);
    CHECK(a.ptr != nullptr);
    CHECK_EQ(reinterpret_cast<size_t>(a.ptr) % align, size_t(0));
    mem::sys::free(a.ptr, 300, align);
  }

  mem::Block g = mem::sys::alloc(16, mem::kMaxAlign);
  static_cast<char*>(g.ptr)[0] = 42;
  mem::Block r = mem::sys::resize(g.ptr, 16, 4096, mem::kMaxAlign);
  if (r.ptr) {  // a backend may answer "cannot"
    CHECK(r.size >= 4096);
    CHECK_EQ(static_cast<char*>(r.ptr)[0], char(42));
    mem::sys::free(r.ptr, 4096, mem::kMaxAlign);
  } else {
    mem::sys::free(g.ptr, 16, mem::kMaxAlign);
  }
}

YASL_TEST(system_resource_reports_real_size) {
  mem::Block b = mem::system_resource()->allocate_at_least(50);
  CHECK(b.size >= 50);
  mem::system_resource()->deallocate(b.ptr, 50);
  mem::Block big = mem::system_resource()->allocate_at_least(10, 4096);
  CHECK_EQ(reinterpret_cast<size_t>(big.ptr) % 4096, size_t(0));
  mem::system_resource()->deallocate(big.ptr, 10, 4096);
}

namespace {
// A block that counts what reaches it, for routing tests.
struct CountingBlock {
  static inline int allocs = 0;
  static inline int frees = 0;

  mem::Block alloc(size_t size, size_t align) noexcept {
    ++allocs;
    return mem::SysBlock().alloc(size, align);
  }

  void free(void* p, size_t size, size_t align) noexcept {
    ++frees;
    mem::SysBlock().free(p, size, align);
  }

  mem::Block resize(void*, size_t, size_t, size_t) noexcept { return {}; }

  bool owns(const void*) const noexcept { return true; }
};
}  // namespace

YASL_TEST(compose_stack_buffer) {
  mem::StackBuffer<256> sb;
  mem::Block a = sb.alloc(96, 8);
  CHECK(a.ptr != nullptr);
  CHECK(sb.owns(a.ptr));
  mem::Block b = sb.alloc(96, 8);
  CHECK(sb.owns(b.ptr));
  CHECK(sb.alloc(96, 8).ptr == nullptr);  // does not fit
  // LIFO rollback and in-place growth of the newest block.
  sb.free(b.ptr, 96, 8);
  CHECK_EQ(sb.used(), size_t(96));
  mem::Block g = sb.resize(a.ptr, 96, 200, 8);
  CHECK(g.ptr == a.ptr);
  CHECK(sb.resize(a.ptr, 200, 400, 8).ptr == nullptr);
  int local = 0;
  CHECK(!sb.owns(&local));
}

YASL_TEST(compose_fallback_routing) {
  CountingBlock::allocs = CountingBlock::frees = 0;
  mem::Fallback<mem::StackBuffer<128>, CountingBlock> f;
  mem::Block small = f.alloc(64, 8);  // stack
  mem::Block big = f.alloc(1000, 8);  // does not fit: secondary
  CHECK_EQ(CountingBlock::allocs, 1);
  CHECK(f.owns(small.ptr));
  f.free(small.ptr, 64, 8);
  CHECK_EQ(CountingBlock::frees, 0);  // went to the stack buffer
  f.free(big.ptr, 1000, 8);
  CHECK_EQ(CountingBlock::frees, 1);
}

YASL_TEST(compose_segregator_routing) {
  CountingBlock::allocs = CountingBlock::frees = 0;
  mem::Segregator<64, mem::StackBuffer<256>, CountingBlock> s;
  mem::Block small = s.alloc(32, 8);
  CHECK_EQ(CountingBlock::allocs, 0);
  mem::Block large = s.alloc(65, 8);
  CHECK_EQ(CountingBlock::allocs, 1);
  s.free(large.ptr, 65, 8);
  CHECK_EQ(CountingBlock::frees, 1);
  s.free(small.ptr, 32, 8);
  CHECK_EQ(CountingBlock::frees, 1);
  // Crossing the threshold is not an in-place resize.
  mem::Block m = s.alloc(32, 8);
  CHECK(s.resize(m.ptr, 32, 100, 8).ptr == nullptr);
  // A small backend that reports more than the threshold is clamped.
  mem::Segregator<16, mem::SysBlock, mem::SysBlock> c;
  mem::Block cb = c.alloc(10, 8);
  CHECK(cb.size <= 16);
  c.free(cb.ptr, cb.size, 8);
}

YASL_TEST(compose_as_resource_and_sizes) {
  static_assert(sizeof(mem::SysBlock) == 1);
  // Two members of one empty type must have distinct addresses, so the
  // empty-composition check uses distinct types.
  static_assert(sizeof(mem::Fallback<mem::SysBlock, CountingBlock>) == 1);
  static_assert(sizeof(mem::Segregator<64, mem::SysBlock, CountingBlock>) == 1);

  {
    mem::AsResource<mem::Fallback<mem::StackBuffer<512>, mem::SysBlock>> r;
    Vector<int> v(&r);
    for (int i = 0; i < 1000; ++i) {
      v.push_back(i);  // starts on the stack, ends on the heap
    }
    CHECK_EQ(v[999], 999);
  }
  mem::AsResource<mem::StackBuffer<16>> tiny;
  CHECK_THROWS(tiny.allocate(64), BadAlloc);
}

YASL_TEST(frame_stack_first) {
  mem::Tracking up;
  {
    mem::ScopedDefault outer(&up);
    mem::Frame<4096> f;
    CHECK(mem::default_resource() == f.resource());
    {
      Vector<int> v;
      for (int i = 0; i < 100; ++i) {
        v.push_back(i);
      }
      CHECK_EQ(v[99], 99);
      CHECK(f.used() > 0);
    }
    CHECK_EQ(up.stats().allocations, size_t(0));  // all on the stack
  }
  CHECK(mem::default_resource() != &up);
}

YASL_TEST(frame_overflow_goes_upstream) {
  mem::Tracking up;
  {
    mem::ScopedDefault outer(&up);
    mem::Frame<256> f;
    Vector<int> v;
    for (int i = 0; i < 1000; ++i) {
      v.push_back(i);
    }
    CHECK(up.stats().allocations > 0);
    CHECK_EQ(v[999], 999);
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

#ifndef NDEBUG
YASL_TEST(frame_detects_escaped_block) {
  mem::Frame<256> f;
  void* leaked = f.resource()->allocate(16);
  CHECK_PANICS(f.verify_no_live());
  f.resource()->deallocate(leaked, 16);
  f.verify_no_live();
}
#endif

YASL_TEST(mem_object_sizes) {
  // A Pool keeps one free list per class and one shared bump region.
  static_assert(sizeof(mem::Pool) <= 8 + 8 + 16 + 8 + 16 + 28 * 8);
  static_assert(sizeof(mem::Arena) <= 8 + 8 + 16 + 8 + 8 + 24 + 8);
  static_assert(sizeof(mem::InlineArena<256>) - 256 <= sizeof(mem::Arena) + 16);
}

namespace {
// Not trivially relocatable as far as the library knows: it counts its moves.
struct Counted {
  static inline int moves = 0;
  int v;

  explicit Counted(int x) : v(x) {}

  Counted(Counted&& o) noexcept : v(o.v) { ++moves; }

  Counted(const Counted& o) : v(o.v) { ++moves; }

  ~Counted() {}
};
}  // namespace

YASL_TEST(resize_vector_grows_in_place_in_arena) {
  mem::Tracking up;
  mem::Arena arena(&up, 1 << 20);
  {
    Vector<int> v(&arena);
    for (int i = 0; i < 10000; ++i) {
      v.push_back(i);
    }
    CHECK_EQ(v[9999], 9999);
    // Every growth extended the same block: the arena holds about the final
    // capacity, not the sum of all the doublings.
    CHECK(arena.used() <= v.capacity() * sizeof(int) + 64);
  }
}

YASL_TEST(resize_skips_nontrivial_types) {
  mem::Tracking t;
  {
    Vector<Counted> v(&t);
    for (int i = 0; i < 100; ++i) {
      v.emplace_back(i);
    }
    CHECK_EQ(t.stats().resizes, size_t(0));
    CHECK(Counted::moves > 0);
    CHECK_EQ(v[99].v, 99);
  }
  mem::Tracking t2;
  {
    Vector<String> v(&t2);  // String is declared trivially relocatable
    for (int i = 0; i < 100; ++i) {
      v.push_back(String("some longer string to avoid nothing"));
    }
    CHECK_EQ(v[99], "some longer string to avoid nothing");
  }
  CHECK_EQ(t2.stats().live(), size_t(0));
}

YASL_TEST(resize_alias_push_back_survives_growth) {
  mem::Tracking t;
  {
    Vector<int> v(&t);
    v.push_back(7);
    for (int i = 0; i < 1000; ++i) {
      v.push_back(v[0]);  // the argument lives in the block being grown
    }
    CHECK_EQ(v[1000], 7);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(resize_string_and_resource_fallbacks) {
  mem::Tracking t;
  {
    String s(&t);
    for (int i = 0; i < 1000; ++i) {
      s.push_back('x');
    }
    CHECK_EQ(s.size(), size_t(1000));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
  // A resource that cannot resize says so.
  CHECK(mem::null_resource()->resize(nullptr, 0, 8).ptr == nullptr);
  mem::Pool pool;
  void* p = pool.allocate(16);
  CHECK(pool.resize(p, 16, 32, mem::kMaxAlign, true).ptr == nullptr);
  pool.deallocate(p, 16);
}
