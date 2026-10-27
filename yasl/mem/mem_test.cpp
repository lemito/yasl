#include <yasl/compat/allocator.h>
#include <yasl/compat/pmr.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/arena.h>
#include <yasl/mem/pool.h>
#include <yasl/mem/sync.h>
#include <yasl/mem/tracking.h>
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
    CHECK_EQ(mem::default_resource(), mem::system_resource());
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
