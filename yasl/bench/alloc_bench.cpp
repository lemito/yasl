#include <stdlib.h>
#include <yasl/bench/harness.h>
#include <yasl/mem/arena.h>
#include <yasl/mem/obj_pool.h>
#include <yasl/mem/pool.h>
#include <yasl/mem/resource.h>
#include <yasl/mem/thread_local_pool.h>
#include <yasl/rand/random.h>

#include <atomic>
#include <memory_resource>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace bench {
namespace {

using namespace yasl;

constexpr size_t kOps = 200000;

struct MallocImpl {
  void* alloc(size_t n) { return malloc(n); }
  void free(void* p, size_t) { ::free(p); }
};

struct NewImpl {
  void* alloc(size_t n) { return ::operator new(n); }
  void free(void* p, size_t) { ::operator delete(p); }
};

struct ResourceImpl {
  mem::Resource* res;
  void* alloc(size_t n) { return res->allocate(n, 8); }
  void free(void* p, size_t n) { res->deallocate(p, n, 8); }
};

struct OwnedPoolImpl {
  mem::Pool pool{mem::system_resource()};
  void* alloc(size_t n) { return pool.allocate(n, 8); }
  void free(void* p, size_t n) { pool.deallocate(p, n, 8); }
};

template <class R>
struct PmrImpl {
  R res;
  void* alloc(size_t n) { return res.allocate(n, 8); }
  void free(void* p, size_t n) { res.deallocate(p, n, 8); }
};

template <class Impl>
void churn(Impl& a, uint64 seed) {
  constexpr size_t kWindow = 1024;
  void* live[kWindow] = {};
  size_t sizes[kWindow] = {};
  Random rng(seed);
  for (size_t i = 0; i < kOps; ++i) {
    size_t slot = i % kWindow;
    if (live[slot]) {
      a.free(live[slot], sizes[slot]);
    }
    sizes[slot] = 8 + rng.next_u64() % 504;
    live[slot] = a.alloc(sizes[slot]);
  }
  for (size_t i = 0; i < kWindow; ++i) {
    if (live[i]) {
      a.free(live[i], sizes[i]);
    }
  }
}

template <class Make>
void churn_case(Bencher& b, std::string_view group, const char* name,
                int threads, Make make) {
  b.run_measured(group, name, kOps * size_t(threads), [&] {
    auto impl = make();
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
      pool.emplace_back([&, t] {
        churn(impl, uint64(t));
        ready.fetch_add(1);
        while (!go.load(std::memory_order_acquire)) {
        }
        churn(impl, uint64(t) + 100);
      });
    }
    while (ready.load() != threads) {
    }
    Instant start = Instant::now();
    go.store(true, std::memory_order_release);
    for (auto& th : pool) {
      th.join();
    }
    return start.elapsed();
  });
}

void small_blocks(Bencher& b) {
  std::string g = "small alloc/free 8-511 B, 1 thread";
  churn_case(b, g, "malloc/free", 1, [] { return MallocImpl{}; });
  churn_case(b, g, "operator new/delete", 1, [] { return NewImpl{}; });
  churn_case(b, g, "yasl sys (libc)", 1,
             [] { return ResourceImpl{mem::system_resource()}; });
  churn_case(b, g, "yasl chain (thread pools)", 1,
             [] { return ResourceImpl{mem::chain_resource()}; });
  churn_case(b, g, "yasl mem::Pool over sys", 1,
             [] { return OwnedPoolImpl{}; });
  churn_case(b, g, "std::pmr::unsynchronized_pool", 1,
             [] { return PmrImpl<std::pmr::unsynchronized_pool_resource>{}; });

  for (int threads : {4, 8}) {
    std::string mt = "small alloc/free 8-511 B, " + std::to_string(threads) +
                     " threads (total ops)";
    churn_case(b, mt, "malloc/free", threads, [] { return MallocImpl{}; });
    churn_case(b, mt, "operator new/delete", threads, [] { return NewImpl{}; });
    churn_case(b, mt, "yasl sys (libc)", threads,
               [] { return ResourceImpl{mem::system_resource()}; });
    churn_case(b, mt, "yasl chain (thread pools)", threads,
               [] { return ResourceImpl{mem::chain_resource()}; });
    churn_case(b, mt, "std::pmr::synchronized_pool", threads,
               [] { return PmrImpl<std::pmr::synchronized_pool_resource>{}; });
  }
}

void node_lists(Bencher& b) {
  std::string_view g = "build 200k-node list, free all";
  b.run(g, "new/delete", kOps, [&] {
    Node* head = nullptr;
    for (uint64 k = 0; k < kOps; ++k) {
      head = new Node(k, head);
    }
    keep(head);
    while (head) {
      Node* next = head->next;
      delete head;
      head = next;
    }
  });
  b.run(g, "yasl ObjPool::make", kOps, [&] {
    mem::ObjPool pool;
    Node* head = nullptr;
    for (uint64 k = 0; k < kOps; ++k) {
      head = pool.make<Node>(k, head);
    }
    keep(head);
  });
  b.run(g, "yasl Arena::create", kOps, [&] {
    mem::Arena arena(mem::system_resource());
    Node* head = nullptr;
    for (uint64 k = 0; k < kOps; ++k) {
      head = arena.create<Node>(k, head);
    }
    keep(head);
  });
  b.run(g, "std::pmr::monotonic_buffer", kOps, [&] {
    std::pmr::monotonic_buffer_resource mr;
    Node* head = nullptr;
    for (uint64 k = 0; k < kOps; ++k) {
      void* p = mr.allocate(sizeof(Node), alignof(Node));
      head = new (p) Node(k, head);
    }
    keep(head);
  });
}

}  // namespace

void alloc_benches(Bencher& b) {
  small_blocks(b);
  node_lists(b);
}

}  // namespace bench
