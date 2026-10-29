#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>
#include <yasl/algo/algo.h>
#include <yasl/bench/harness.h>
#include <yasl/cont/adapter/priority_queue.h>
#include <yasl/cont/assoc/hash_map.h>
#include <yasl/cont/assoc/map.h>
#include <yasl/cont/assoc/string_map.h>
#include <yasl/cont/seq/bitset.h>
#include <yasl/cont/seq/deque.h>
#include <yasl/cont/seq/list.h>
#include <yasl/cont/seq/small_vector.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/resource.h>
#include <yasl/mem/thread_local_pool.h>
#include <yasl/rand/random.h>
#include <yasl/str/str.h>
#include <yasl/str/view.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace bench {
namespace {

using namespace yasl;

constexpr size_t kOps = 200000;

template <class M, class K>
bool contains_key(const M& m, const K& k) {
  if constexpr (requires { m.get(k); }) {
    return m.get(k) != nullptr;
  } else {
    return m.find(k) != m.end();
  }
}

template <class M, class Fill>
M prebuilt(bool needed, Fill fill) {
  M m;
  if (needed) {
    fill(m);
  }
  return m;
}

void sequences(Bencher& b) {
  {
    constexpr size_t kBig = 20'000'000;
    std::string_view g = "push_back 20M u64";
    b.run(
        g, "std::vector (reserved)", kBig,
        [&] {
          std::vector<uint64> v;
          v.reserve(kBig);
          for (size_t i = 0; i < kBig; ++i) {
            v.push_back(i);
          }
          keep(v.size());
        },
        3);
    b.run(
        g, "std::vector", kBig,
        [&] {
          std::vector<uint64> v;
          for (size_t i = 0; i < kBig; ++i) {
            v.push_back(i);
          }
          keep(v.size());
        },
        3);
    b.run(
        g, "yasl::Vector (sys)", kBig,
        [&] {
          Vector<uint64> v(mem::system_resource());
          for (size_t i = 0; i < kBig; ++i) {
            v.push_back(i);
          }
          keep(v.size());
        },
        3);
    b.run(
        g, "yasl::Vector (chain)", kBig,
        [&] {
          Vector<uint64> v(mem::chain_resource());
          for (size_t i = 0; i < kBig; ++i) {
            v.push_back(i);
          }
          keep(v.size());
        },
        3);
  }

  {
    constexpr size_t kN = 1'000'000;
    auto keys = make_keys(kN);
    std::string_view g = "sort 1M random u64";
    b.run(
        g, "std::sort", kN,
        [&] {
          std::vector<uint64> v = keys;
          std::sort(v.begin(), v.end());
          keep(v[kN / 2]);
        },
        3);
    b.run(
        g, "yasl::sort", kN,
        [&] {
          Vector<uint64> v(keys.begin(), keys.end(), mem::system_resource());
          yasl::sort(v.begin(), v.end());
          keep(v[kN / 2]);
        },
        3);
  }

  auto small = [&](std::string_view g, size_t n) {
    b.run(g, "std::vector", kOps, [&] {
      for (size_t i = 0; i < kOps; ++i) {
        std::vector<uint64> v;
        for (size_t j = 0; j < n; ++j) {
          v.push_back(j);
        }
        keep(v.size());
      }
    });
    b.run(g, "absl::InlinedVector<8>", kOps, [&] {
      for (size_t i = 0; i < kOps; ++i) {
        absl::InlinedVector<uint64, 8> v;
        for (size_t j = 0; j < n; ++j) {
          v.push_back(j);
        }
        keep(v.size());
      }
    });
    b.run(g, "yasl::SmallVector<8>", kOps, [&] {
      for (size_t i = 0; i < kOps; ++i) {
        SmallVector<uint64, 8> v;
        for (size_t j = 0; j < n; ++j) {
          v.push_back(j);
        }
        keep(v.size());
      }
    });
  };
  small("small vector: 8 pushes, 200k times", 8);
  small("small vector: 32 pushes, 200k times", 32);

  {
    constexpr size_t kN = 1'000'000;
    std::string_view g = "queue: push_back + pop_front, 1M (window 64)";
    b.run(g, "std::deque", kN, [&] {
      std::deque<uint64> q;
      uint64 s = 0;
      for (size_t i = 0; i < kN; ++i) {
        q.push_back(i);
        if (q.size() > 64) {
          s += q.front();
          q.pop_front();
        }
      }
      keep(s);
    });
    b.run(g, "yasl::Deque", kN, [&] {
      Deque<uint64> q;
      uint64 s = 0;
      for (size_t i = 0; i < kN; ++i) {
        q.push_back(i);
        if (q.size() > 64) {
          s += q.front();
          q.pop_front();
        }
      }
      keep(s);
    });
  }

  {
    std::string_view g = "list: push_back 200k, then iterate";
    b.run(g, "std::list", kOps, [&] {
      std::list<uint64> l;
      for (size_t i = 0; i < kOps; ++i) {
        l.push_back(i);
      }
      uint64 s = 0;
      for (uint64 x : l) {
        s += x;
      }
      keep(s);
    });
    b.run(g, "yasl::List", kOps, [&] {
      List<uint64> l;
      for (size_t i = 0; i < kOps; ++i) {
        l.push_back(i);
      }
      uint64 s = 0;
      for (uint64 x : l) {
        s += x;
      }
      keep(s);
    });
  }

  {
    auto keys = make_keys(kOps);
    std::string_view g = "priority queue: push 200k, pop all (per element)";
    b.run(g, "std::priority_queue", kOps, [&] {
      std::priority_queue<uint64, std::vector<uint64>, std::greater<>> pq;
      for (uint64 k : keys) {
        pq.push(k);
      }
      uint64 s = 0;
      while (!pq.empty()) {
        s += pq.top();
        pq.pop();
      }
      keep(s);
    });
    b.run(g, "yasl::PriorityQueue", kOps, [&] {
      PriorityQueue<uint64> pq;
      for (uint64 k : keys) {
        pq.push(k);
      }
      uint64 s = 0;
      while (pq.size() > 0) {
        s += pq.top();
        pq.pop();
      }
      keep(s);
    });
  }

  {
    constexpr size_t kBits = 1 << 20;
    constexpr size_t kSets = 1'000'000;
    Random rng(5);
    std::vector<size_t> idx(kSets);
    for (auto& i : idx) {
      i = rng.next_u64() % kBits;
    }
    std::string_view g = "bitset: 1M random sets, then test 1M bits";
    b.run(g, "std::vector<bool>", kSets, [&] {
      std::vector<bool> bits(kBits);
      for (size_t i : idx) {
        bits[i] = true;
      }
      size_t hits = 0;
      for (size_t i = 0; i < kBits; ++i) {
        hits += bits[i];
      }
      keep(hits);
    });
    b.run(g, "yasl::BitSet", kSets, [&] {
      BitSet bits(kBits);
      for (size_t i : idx) {
        bits.set(i);
      }
      size_t hits = 0;
      for (size_t i = 0; i < kBits; ++i) {
        hits += bits.test(i);
      }
      keep(hits);
    });
  }
}

void integer_maps(Bencher& b) {
  auto keys = make_keys(kOps);

  {
    std::string_view g = "ordered map insert 200k u64";
    b.run(g, "yasl::BTreeMap", kOps, [&] {
      BTreeMap<uint64, uint64> m;
      for (uint64 k : keys) {
        m[k] = k;
      }
      keep(m.size());
    });
    b.run(g, "absl::btree_map", kOps, [&] {
      absl::btree_map<uint64, uint64> m;
      for (uint64 k : keys) {
        m[k] = k;
      }
      keep(m.size());
    });
    b.run(g, "std::map", kOps, [&] {
      std::map<uint64, uint64> m;
      for (uint64 k : keys) {
        m[k] = k;
      }
      keep(m.size());
    });
    b.run(g, "yasl::TreapMap", kOps, [&] {
      TreapMap<uint64, uint64> m;
      for (uint64 k : keys) {
        m[k] = k;
      }
      keep(m.size());
    });
  }

  {
    std::string_view g = "hash map insert 200k u64";
    b.run(g, "yasl::HashMap", kOps, [&] {
      HashMap<uint64, uint64> m;
      for (uint64 k : keys) {
        m[k] = k;
      }
      keep(m.size());
    });
    b.run(g, "absl::flat_hash_map", kOps, [&] {
      absl::flat_hash_map<uint64, uint64> m;
      for (uint64 k : keys) {
        m[k] = k;
      }
      keep(m.size());
    });
    b.run(g, "std::unordered_map", kOps, [&] {
      std::unordered_map<uint64, uint64> m;
      for (uint64 k : keys) {
        m[k] = k;
      }
      keep(m.size());
    });
  }

  constexpr std::string_view kOrdFind = "ordered map find 200k u64 (hits)";
  constexpr std::string_view kHashFind = "hash map find 200k u64 (hits)";
  constexpr std::string_view kIter = "ordered map iterate (full scan)";
  auto fill = [&](auto& m) {
    for (uint64 k : keys) {
      m[k] = k;
    }
  };
  auto ybt = prebuilt<BTreeMap<uint64, uint64>>(
      b.wants(kOrdFind, "yasl::BTreeMap") || b.wants(kIter, "yasl::BTreeMap"),
      fill);
  auto abt = prebuilt<absl::btree_map<uint64, uint64>>(
      b.wants(kOrdFind, "absl::btree_map") || b.wants(kIter, "absl::btree_map"),
      fill);
  auto sbt = prebuilt<std::map<uint64, uint64>>(
      b.wants(kOrdFind, "std::map") || b.wants(kIter, "std::map"), fill);
  auto ytr = prebuilt<TreapMap<uint64, uint64>>(
      b.wants(kOrdFind, "yasl::TreapMap"), fill);
  auto yhm = prebuilt<HashMap<uint64, uint64>>(
      b.wants(kHashFind, "yasl::HashMap"), fill);
  auto ahm = prebuilt<absl::flat_hash_map<uint64, uint64>>(
      b.wants(kHashFind, "absl::flat_hash_map"), fill);
  auto uhm = prebuilt<std::unordered_map<uint64, uint64>>(
      b.wants(kHashFind, "std::unordered_map"), fill);

  {
    std::string_view g = kOrdFind;
    b.run(g, "yasl::BTreeMap", kOps, [&] {
      size_t hits = 0;
      for (uint64 k : keys) {
        hits += contains_key(ybt, k);
      }
      keep(hits);
    });
    b.run(g, "absl::btree_map", kOps, [&] {
      size_t hits = 0;
      for (uint64 k : keys) {
        hits += contains_key(abt, k);
      }
      keep(hits);
    });
    b.run(g, "std::map", kOps, [&] {
      size_t hits = 0;
      for (uint64 k : keys) {
        hits += contains_key(sbt, k);
      }
      keep(hits);
    });
    b.run(g, "yasl::TreapMap", kOps, [&] {
      size_t hits = 0;
      for (uint64 k : keys) {
        hits += contains_key(ytr, k);
      }
      keep(hits);
    });
  }

  {
    std::string_view g = kHashFind;
    b.run(g, "yasl::HashMap", kOps, [&] {
      size_t hits = 0;
      for (uint64 k : keys) {
        hits += contains_key(yhm, k);
      }
      keep(hits);
    });
    b.run(g, "absl::flat_hash_map", kOps, [&] {
      size_t hits = 0;
      for (uint64 k : keys) {
        hits += contains_key(ahm, k);
      }
      keep(hits);
    });
    b.run(g, "std::unordered_map", kOps, [&] {
      size_t hits = 0;
      for (uint64 k : keys) {
        hits += contains_key(uhm, k);
      }
      keep(hits);
    });
  }

  {
    std::string_view g = kIter;
    b.run(g, "yasl::BTreeMap", ybt.size(), [&] {
      uint64 s = 0;
      for (auto& e : ybt) {
        s += e.value;
      }
      keep(s);
    });
    b.run(g, "absl::btree_map", abt.size(), [&] {
      uint64 s = 0;
      for (auto& [k, v] : abt) {
        s += v;
      }
      keep(s);
    });
    b.run(g, "std::map", sbt.size(), [&] {
      uint64 s = 0;
      for (auto& [k, v] : sbt) {
        s += v;
      }
      keep(s);
    });
  }
}

void string_maps(Bencher& b) {
  auto strs = make_strings(kOps);

  {
    std::string_view g = "string-key insert 200k";
    b.run(g, "yasl::StringMap (radix)", kOps, [&] {
      StringMap<int> m;
      for (const auto& s : strs) {
        m[StringView(s.data(), s.size())] = 1;
      }
      keep(m.size());
    });
    b.run(g, "yasl::HashMap<String>", kOps, [&] {
      HashMap<String, int> m;
      for (const auto& s : strs) {
        m[String(StringView(s.data(), s.size()))] = 1;
      }
      keep(m.size());
    });
    b.run(g, "absl::flat_hash_map<string>", kOps, [&] {
      absl::flat_hash_map<std::string, int> m;
      for (const auto& s : strs) {
        m[s] = 1;
      }
      keep(m.size());
    });
    b.run(g, "std::unordered_map<string>", kOps, [&] {
      std::unordered_map<std::string, int> m;
      for (const auto& s : strs) {
        m[s] = 1;
      }
      keep(m.size());
    });
    b.run(g, "std::map<string>", kOps, [&] {
      std::map<std::string, int> m;
      for (const auto& s : strs) {
        m[s] = 1;
      }
      keep(m.size());
    });
  }

  constexpr std::string_view kFind = "string-key find 200k (hits)";
  auto fill_std = [&](auto& m) {
    for (const auto& s : strs) {
      m[s] = 1;
    }
  };
  auto ysm = prebuilt<StringMap<int>>(b.wants(kFind, "yasl::StringMap (radix)"),
                                      [&](StringMap<int>& m) {
                                        for (const auto& s : strs) {
                                          m[StringView(s.data(), s.size())] = 1;
                                        }
                                      });
  auto yhm = prebuilt<HashMap<String, int>>(
      b.wants(kFind, "yasl::HashMap<String>"), [&](HashMap<String, int>& m) {
        for (const auto& s : strs) {
          m[String(StringView(s.data(), s.size()))] = 1;
        }
      });
  auto ahm = prebuilt<absl::flat_hash_map<std::string, int>>(
      b.wants(kFind, "absl::flat_hash_map<string>"), fill_std);
  auto uhm = prebuilt<std::unordered_map<std::string, int>>(
      b.wants(kFind, "std::unordered_map<string>"), fill_std);
  auto sm = prebuilt<std::map<std::string, int>>(
      b.wants(kFind, "std::map<string>"), fill_std);

  {
    std::string_view g = kFind;
    b.run(g, "yasl::StringMap (radix)", kOps, [&] {
      size_t hits = 0;
      for (const auto& s : strs) {
        hits += ysm.get(StringView(s.data(), s.size())) != nullptr;
      }
      keep(hits);
    });
    b.run(g, "yasl::HashMap<String>", kOps, [&] {
      size_t hits = 0;
      for (const auto& s : strs) {
        hits += yhm.get(StringView(s.data(), s.size())) != nullptr;
      }
      keep(hits);
    });
    b.run(g, "absl::flat_hash_map<string>", kOps, [&] {
      size_t hits = 0;
      for (const auto& s : strs) {
        hits += ahm.find(s) != ahm.end();
      }
      keep(hits);
    });
    b.run(g, "std::unordered_map<string>", kOps, [&] {
      size_t hits = 0;
      for (const auto& s : strs) {
        hits += uhm.find(s) != uhm.end();
      }
      keep(hits);
    });
    b.run(g, "std::map<string>", kOps, [&] {
      size_t hits = 0;
      for (const auto& s : strs) {
        hits += sm.find(s) != sm.end();
      }
      keep(hits);
    });
  }
}

}  // namespace

void cont_benches(Bencher& b) {
  sequences(b);
  integer_maps(b);
  string_maps(b);
}

}  // namespace bench
