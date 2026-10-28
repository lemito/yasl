#include <yasl/conc/channel.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/thread_local_pool.h>
#include <yasl/test/test.h>

#include <atomic>
#include <map>
#include <random>

using namespace yasl;

namespace {
struct Item {
  unsigned char* p;
  size_t n;
  unsigned char tag;
};

void fill(const Item& it) {
  for (size_t i = 0; i < it.n; ++i) {
    it.p[i] = static_cast<unsigned char>(it.tag + i);
  }
}

bool intact(const Item& it) {
  for (size_t i = 0; i < it.n; ++i) {
    if (it.p[i] != static_cast<unsigned char>(it.tag + i)) {
      return false;
    }
  }
  return true;
}
}  // namespace

YASL_TEST(tlpool_classes_and_alignment) {
  mem::ThreadLocalPool pool;
  size_t last = 0;
  for (size_t n = 1; n <= mem::ThreadLocalPool::kMaxSmall; ++n) {
    mem::Block b = pool.alloc(n, 8);
    CHECK(b.ptr != nullptr);
    CHECK(b.size >= n);
    CHECK_EQ(reinterpret_cast<uintptr_t>(b.ptr) % 16, uintptr_t(0));
    // At most 25% waste above the 128-byte classes.
    if (n > 128) {
      CHECK(b.size * 4 <= n * 5 + 4);
    }
    CHECK(b.size >= last || n == 1);
    last = b.size;
    pool.free(b.ptr, n, 8);
  }
  CHECK(pool.alloc(mem::ThreadLocalPool::kMaxSmall + 1, 8).ptr == nullptr);
  CHECK(pool.alloc(16, 64).ptr == nullptr);
}

YASL_TEST(tlpool_random_no_overlap) {
  mem::ThreadLocalPool pool;
  std::mt19937 rng(7);
  Vector<Item> live;
  std::map<unsigned char*, size_t> ranges;
  for (int step = 0; step < 100000; ++step) {
    if (live.empty() || rng() % 100 < 55) {
      size_t n = 1 + rng() % 600;
      if (rng() % 50 == 0) {
        n = 1 + rng() % 4096;
      }
      mem::Block b = pool.alloc(n, 8);
      Item it{static_cast<unsigned char*>(b.ptr), n,
              static_cast<unsigned char>(rng())};
      fill(it);
      live.push_back(it);
    } else {
      size_t i = rng() % live.size();
      CHECK(intact(live[i]));  // nobody wrote over it
      pool.free(live[i].p, live[i].n, 8);
      live[i] = live.back();
      live.pop_back();
    }
  }
  for (Item& it : live) {
    CHECK(intact(it));
    pool.free(it.p, it.n, 8);
  }
}

YASL_TEST(tlpool_cross_thread_channel) {
  mem::ThreadLocalPool pool;
  constexpr int kProducers = 4;
  constexpr int kPerProducer = 20000;
  auto [tx, rx] = make_channel<Item>(64);
  std::atomic<int> bad{0};
  std::atomic<int> got{0};
  {
    Vector<Thread> threads;
    for (int t = 0; t < kProducers; ++t) {
      threads.push_back(Thread([&, tx = tx, t]() mutable {
        std::mt19937 rng(static_cast<unsigned>(t));
        for (int i = 0; i < kPerProducer; ++i) {
          size_t n = 1 + rng() % 300;
          Item it{static_cast<unsigned char*>(pool.alloc(n, 8).ptr), n,
                  static_cast<unsigned char>(i)};
          fill(it);
          (void)tx.send(it);
        }
        // The thread ends here, with blocks still in the channel.
      }));
    }
    threads.push_back(Thread([&, rx = yasl::move(rx)]() mutable {
      while (auto it = rx.recv()) {
        bad += !intact(*it);
        pool.free(it->p, it->n, 8);
        ++got;
        if (got.load() == kProducers * kPerProducer) {
          break;
        }
      }
    }));
  }
  CHECK_EQ(bad.load(), 0);
  CHECK_EQ(got.load(), kProducers * kPerProducer);
}

YASL_TEST(tlpool_thread_exit_with_live_blocks) {
  mem::ThreadLocalPool pool;
  Vector<Item> items;
  {
    Thread t([&] {
      for (int i = 0; i < 1000; ++i) {
        Item it{static_cast<unsigned char*>(pool.alloc(64, 8).ptr), 64,
                static_cast<unsigned char>(i)};
        fill(it);
        items.push_back(it);
      }
    });
  }
  // The owner is gone; its slabs belong to the shared heap now.
  for (Item& it : items) {
    CHECK(intact(it));
    pool.free(it.p, it.n, 8);
  }
  // A new thread still allocates fine afterwards.
  Thread t([&] {
    mem::Block b = pool.alloc(64, 8);
    pool.free(b.ptr, 64, 8);
  });
}

YASL_TEST(tlpool_chain_resource) {
  mem::Resource* r = mem::chain_resource();
  void* small = r->allocate(100, 8);
  void* aligned = r->allocate(100, 64);
  void* big = r->allocate(100000, 8);
  CHECK_EQ(reinterpret_cast<uintptr_t>(aligned) % 64, uintptr_t(0));
  r->deallocate(big, 100000, 8);
  r->deallocate(aligned, 100, 64);
  r->deallocate(small, 100, 8);
  Vector<int> v(r);
  for (int i = 0; i < 10000; ++i) {
    v.push_back(i);
  }
  CHECK_EQ(v[9999], 9999);
}
