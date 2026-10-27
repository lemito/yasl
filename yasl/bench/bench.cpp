// Micro-benchmarks: yasl containers and formatting against the standard
// library. Build with -DYASL_BUILD_BENCH=ON (Release!) and run
// ./build/yasl_bench [filter].

#include <stdio.h>
#include <string.h>
#include <yasl/cont/assoc/hash_map.h>
#include <yasl/cont/assoc/map.h>
#include <yasl/cont/assoc/string_map.h>
#include <yasl/io/fmt.h>
#include <yasl/mem/obj_pool.h>
#include <yasl/rand/random.h>
#include <yasl/str/builder.h>
#include <yasl/time/time.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace yasl;

namespace {
// Small object with a non-trivial destructor, like a syntax-tree node.
struct Node {
  explicit Node(uint64 v, Node* n = nullptr) : value(v), next(n) {}

  ~Node() { keep_alive += value; }

  uint64 value;
  Node* next;
  static inline uint64 keep_alive = 0;
};

template <class T>
void keep(const T& v) {
  asm volatile("" : : "r,m"(v) : "memory");
}

// Runs f() `reps` times, reports the best time per operation.
template <class F>
void bench(const char* name, const char* filter, size_t ops, F&& f,
           int reps = 5) {
  if (filter && !strstr(name, filter)) {
    return;
  }
  Duration best = Duration::max();
  for (int r = 0; r < reps; ++r) {
    Instant t = Instant::now();
    f();
    Duration d = t.elapsed();
    if (d < best) {
      best = d;
    }
  }
  double ns = double(best.as_nanos()) / double(ops);
  printf("%-34s %9.1f ns/op  (%s total)\n", name, ns, to_string(best).c_str());
}

std::vector<uint64> make_keys(size_t n) {
  Random rng(1);
  std::vector<uint64> keys(n);
  for (auto& k : keys) {
    k = rng.next_u64();
  }
  return keys;
}

std::vector<std::string> make_strings(size_t n) {
  Random rng(2);
  std::vector<std::string> out(n);
  for (auto& s : out) {
    s = "key-" + std::to_string(rng.next_u64());
  }
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  constexpr size_t N = 200000;
  auto keys = make_keys(N);
  auto strs = make_strings(N);

  bench("btreemap insert u64", filter, N, [&] {
    BTreeMap<uint64, uint64> m;
    for (uint64 k : keys) {
      m[k] = k;
    }
    keep(m.size());
  });
  bench("std::map insert u64", filter, N, [&] {
    std::map<uint64, uint64> m;
    for (uint64 k : keys) {
      m[k] = k;
    }
    keep(m.size());
  });
  {
    BTreeMap<uint64, uint64> bm;
    std::map<uint64, uint64> sm;
    HashMap<uint64, uint64> hm;
    std::unordered_map<uint64, uint64> um;
    for (uint64 k : keys) {
      bm[k] = k;
      sm[k] = k;
      hm[k] = k;
      um[k] = k;
    }
    bench("btreemap find u64", filter, N, [&] {
      uint64 s = 0;
      for (uint64 k : keys) {
        s += bm.get(k) ? 1 : 0;
      }
      keep(s);
    });
    bench("std::map find u64", filter, N, [&] {
      uint64 s = 0;
      for (uint64 k : keys) {
        s += sm.find(k) != sm.end() ? 1 : 0;
      }
      keep(s);
    });
    bench("hashmap find u64", filter, N, [&] {
      uint64 s = 0;
      for (uint64 k : keys) {
        s += hm.get(k) ? 1 : 0;
      }
      keep(s);
    });
    bench("std::unordered_map find u64", filter, N, [&] {
      uint64 s = 0;
      for (uint64 k : keys) {
        s += um.find(k) != um.end() ? 1 : 0;
      }
      keep(s);
    });
    bench("btreemap iterate", filter, N, [&] {
      uint64 s = 0;
      for (auto& e : bm) {
        s += e.value;
      }
      keep(s);
    });
    bench("std::map iterate", filter, N, [&] {
      uint64 s = 0;
      for (auto& e : sm) {
        s += e.second;
      }
      keep(s);
    });
  }
  bench("hashmap insert u64", filter, N, [&] {
    HashMap<uint64, uint64> m;
    for (uint64 k : keys) {
      m[k] = k;
    }
    keep(m.size());
  });
  bench("std::unordered_map insert u64", filter, N, [&] {
    std::unordered_map<uint64, uint64> m;
    for (uint64 k : keys) {
      m[k] = k;
    }
    keep(m.size());
  });
  bench("hashmap insert string", filter, N, [&] {
    HashMap<String, int> m;
    for (const auto& s : strs) {
      m[String(s.c_str())] = 1;
    }
    keep(m.size());
  });
  bench("std::unordered_map insert string", filter, N, [&] {
    std::unordered_map<std::string, int> m;
    for (const auto& s : strs) {
      m[s] = 1;
    }
    keep(m.size());
  });

  bench("StringBuilder << ints", filter, N, [&] {
    StringBuilder b;
    for (size_t i = 0; i < N; ++i) {
      b << "id=" << i << ' ';
    }
    keep(b.size());
  });
  bench("std::string append to_string", filter, N, [&] {
    std::string b;
    for (size_t i = 0; i < N; ++i) {
      b += "id=";
      b += std::to_string(i);
      b += ' ';
    }
    keep(b.size());
  });
  bench("yasl format(\"{} {:.2f}\")", filter, N, [&] {
    size_t total = 0;
    for (size_t i = 0; i < N; ++i) {
      total += format("{} {:.2f}", i, double(i) / 7).size();
    }
    keep(total);
  });
  bench("snprintf(\"%zu %.2f\")", filter, N, [&] {
    size_t total = 0;
    char buf[64];
    for (size_t i = 0; i < N; ++i) {
      total += size_t(snprintf(buf, sizeof(buf), "%zu %.2f", i, double(i) / 7));
    }
    keep(total);
  });
  bench("stringmap insert string", filter, N, [&] {
    StringMap<int> m;
    for (const auto& s : strs) {
      m[StringView(s.data(), s.size())] = 1;
    }
    keep(m.size());
  });
  {
    StringMap<int> sm;
    HashMap<String, int> hm;
    for (const auto& s : strs) {
      sm[StringView(s.data(), s.size())] = 1;
      hm[String(s.c_str())] = 1;
    }
    bench("stringmap find string", filter, N, [&] {
      size_t hits = 0;
      for (const auto& s : strs) {
        hits += sm.get(StringView(s.data(), s.size())) ? 1 : 0;
      }
      keep(hits);
    });
    bench("hashmap<String> find string", filter, N, [&] {
      size_t hits = 0;
      for (const auto& s : strs) {
        hits += hm.get(StringView(s.data(), s.size())) ? 1 : 0;
      }
      keep(hits);
    });
    bench("stringmap iterate", filter, N, [&] {
      size_t total = 0;
      for (auto& [key, value] : sm) {
        total += key.size() + size_t(value);
      }
      keep(total);
    });
  }
  bench("ObjPool make<Node>", filter, N, [&] {
    mem::ObjPool pool;
    Node* head = nullptr;
    for (uint64 k : keys) {
      head = pool.make<Node>(k, head);
    }
    keep(head);
  });
  bench("new/delete Node", filter, N, [&] {
    Node* head = nullptr;
    for (uint64 k : keys) {
      head = new Node(k, head);
    }
    keep(head);
    while (head) {
      Node* next = head->next;
      delete head;
      head = next;
    }
  });
  return 0;
}
