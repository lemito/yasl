#pragma once

#include <stdio.h>
#include <string.h>
#include <yasl/rand/random.h>
#include <yasl/str/builder.h>
#include <yasl/time/time.h>

#include <string>
#include <string_view>
#include <vector>

namespace bench {

template <class T>
void keep(const T& v) {
  asm volatile("" : : "r,m"(v) : "memory");
}

class Bencher {
 public:
  Bencher(const char* filter, bool csv) : filter_(filter), csv_(csv) {}

  bool wants(std::string_view group, std::string_view name) const {
    if (!filter_) {
      return true;
    }
    std::string key = std::string(group) + " " + std::string(name);
    return strstr(key.c_str(), filter_) != nullptr;
  }

  template <class F>
  void run(std::string_view group, std::string_view name, size_t ops, F&& f,
           int reps = 5) {
    run_measured(
        group, name, ops,
        [&] {
          yasl::Instant t = yasl::Instant::now();
          f();
          return t.elapsed();
        },
        reps);
  }

  template <class F>
  void run_measured(std::string_view group, std::string_view name, size_t ops,
                    F&& f, int reps = 5) {
    if (!wants(group, name)) {
      return;
    }
    if (group != group_) {
      group_ = std::string(group);
      if (!csv_) {
        printf("\n== %s\n", group_.c_str());
      }
    }
    yasl::Duration best = yasl::Duration::max();
    for (int r = 0; r < reps; ++r) {
      yasl::Duration d = f();
      if (d < best) {
        best = d;
      }
    }
    double ns = double(best.as_nanos()) / double(ops);
    if (csv_) {
      printf("\"%s\",\"%s\",%.2f\n", group_.c_str(), std::string(name).c_str(),
             ns);
    } else {
      printf("  %-36s %10.1f ns/op  (%s total)\n", std::string(name).c_str(),
             ns, yasl::to_string(best).c_str());
    }
  }

 private:
  const char* filter_;
  bool csv_;
  std::string group_;
};

void alloc_benches(Bencher& b);
void cont_benches(Bencher& b);
void str_benches(Bencher& b);

inline std::vector<uint64> make_keys(size_t n, uint64 seed = 1) {
  yasl::Random rng(seed);
  std::vector<uint64> keys(n);
  for (auto& k : keys) {
    k = rng.next_u64();
  }
  return keys;
}

inline std::vector<std::string> make_strings(size_t n, uint64 seed = 2) {
  yasl::Random rng(seed);
  std::vector<std::string> out(n);
  for (auto& s : out) {
    s = "key-" + std::to_string(rng.next_u64());
  }
  return out;
}

struct Node {
  Node(uint64 v, Node* n) : value(v), next(n) {}

  uint64 value;
  Node* next;
};

}  // namespace bench
