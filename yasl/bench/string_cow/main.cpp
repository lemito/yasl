// Standalone experiment: timings and allocation counts use separate runs.
#include <yasl/bench/harness.h>
#include <yasl/compat/allocator.h>
#include <yasl/str/mutstr.h>
#include <yasl/str/str.h>

#include <algorithm>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {
using yasl::String;
using yasl::StringView;
using Clock = std::chrono::steady_clock;
using bench::keep;

struct Cow {
  using Text = String;
  static constexpr const char* name = "yasl-cow";
  static Text make(const std::string& s) { return Text(s.data(), s.size()); }
  static Text copy(const Text& s) { return s; }
  static void mutate(Text& s) { s.set(0, 'z'); }
};
struct Deep : Cow {
  static constexpr const char* name = "yasl-deep-control";
  static Text copy(const Text& s) { return Text(s, s.resource()); }
};
struct Mutable {
  using Text = yasl::MutStr;
  static constexpr const char* name = "yasl-mutable";
  static Text make(const std::string& s) { return Text(s.data(), s.size()); }
  static Text copy(const Text& s) { return s; }
  static void mutate(Text& s) { s[0] = 'z'; }
};
struct Std {
  using Text = std::string;
  static constexpr const char* name = "std-string";
  static Text make(const std::string& s) { return Text(s.data(), s.size()); }
  static Text copy(const Text& s) { return s; }
  static void mutate(Text& s) { s[0] = 'z'; }
};

// Median of seven runs, after one unrecorded warmup. Destruction is timed.
template <class F>
void timing(const char* operation, const char* type, size_t length,
            int mutation, int threads, const char* source, size_t ops, F run) {
  std::vector<double> samples;
  run();
  for (int rep = 0; rep < 7; ++rep) {
    samples.push_back(run() / double(ops));
  }
  std::sort(samples.begin(), samples.end());
  std::printf("%s,%s,%zu,%d,%d,%s,%.3f,%.3f,%.3f\n", operation, type, length,
              mutation, threads, source, samples[3], samples.front(),
              samples.back());
}

template <class Policy>
void serial(size_t length) {
  const size_t n = length >= 4096 ? 20000 : 200000;
  const std::string input(length, 'a');
  const auto source = Policy::make(input);
  timing("construct", Policy::name, length, 0, 1, "bytes", n, [&] {
    const auto start = Clock::now();
    for (size_t i = 0; i < n; ++i) {
      auto s = Policy::make(input);
      keep(s);
    }
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      Clock::now() - start)
                      .count());
  });
  for (int mutation : {0, 1, 10, 100}) {
    if (length == 0 && mutation) continue;
    timing("copy", Policy::name, length, mutation, 1, "shared", n, [&] {
      const auto start = Clock::now();
      for (size_t i = 0; i < n; ++i) {
        auto s = Policy::copy(source);
        if (mutation && i % 100 < size_t(mutation)) Policy::mutate(s);
        keep(s);
      }
      return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - start)
                        .count());
    });
  }
  // Retain batches: exposes allocation/cache costs hidden by immediate reuse
  // of a single freed allocation. Vector storage is reserved outside timing.
  for (int mutation : {0, 10, 100}) {
    if (length == 0 && mutation) continue;
    std::vector<typename Policy::Text> copies;
    copies.reserve(1000);
    timing("retained-copy", Policy::name, length, mutation, 1, "shared", n,
           [&] {
             const auto start = Clock::now();
             for (size_t batch = 0; batch < n / 1000; ++batch) {
               for (size_t i = 0; i < 1000; ++i) {
                 copies.push_back(Policy::copy(source));
                 if (mutation && i % 100 < size_t(mutation))
                   Policy::mutate(copies.back());
               }
               keep(copies);
               copies.clear();
             }
             return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               Clock::now() - start)
                               .count());
           });
  }
  if (length) {
    auto s = Policy::make(input);
    timing("unique-write", Policy::name, length, 100, 1, "unique", n, [&] {
      const auto start = Clock::now();
      for (size_t i = 0; i < n; ++i) {
        Policy::mutate(s);
        keep(s);
      }
      return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - start)
                        .count());
    });
  }
  // Fail fast on semantic errors before trusting any timing results.
  if (length) {
    auto s = Policy::copy(source);
    Policy::mutate(s);
    if (source.data()[0] != 'a' || s.data()[0] != 'z' || s.size() != length)
      std::abort();
  }
}

template <class Policy>
void parallel(size_t length, int threads, bool shared, int mutation) {
  constexpr size_t n = 30000;
  const std::string input(length, 'a');
  const auto common = Policy::make(input);
  timing("parallel-copy", Policy::name, length, mutation, threads,
         shared ? "shared" : "independent", n * threads, [&] {
           // Each independent source is constructed from bytes: no shared COW
           // Rep.
           std::barrier ready(threads + 1), start(threads + 1),
               done(threads + 1);
           std::vector<std::thread> workers;
           workers.reserve(threads);
           for (int t = 0; t < threads; ++t) {
             workers.emplace_back([&] {
               const auto local = Policy::make(input);
               const auto& source = shared ? common : local;
               ready.arrive_and_wait();
               start.arrive_and_wait();
               for (size_t i = 0; i < n; ++i) {
                 auto s = Policy::copy(source);
                 if (mutation && i % 100 < size_t(mutation)) Policy::mutate(s);
                 keep(s);
               }
               done.arrive_and_wait();
             });
           }
           ready.arrive_and_wait();
           const auto begin = Clock::now();
           start.arrive_and_wait();
           done.arrive_and_wait();
           const auto elapsed = Clock::now() - begin;
           for (auto& worker : workers) worker.join();
           return double(
               std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)
                   .count());
         });
}

// Counts requested bytes, not allocator size classes or RSS. Returning exactly
// the requested size makes deallocation accounting unambiguous for both types.
class Counter : public yasl::mem::Resource {
 public:
  size_t allocations = 0, total = 0, live = 0, peak = 0;

 private:
  yasl::mem::Block do_alloc(size_t n, size_t align) override {
    void* p = yasl::mem::system_resource()->allocate(n, align);
    ++allocations;
    total += n;
    live += n;
    peak = std::max(peak, live);
    return {p, n};
  }
  void do_free(void* p, size_t n, size_t align) noexcept override {
    live -= n;
    yasl::mem::system_resource()->deallocate(p, n, align);
  }
};
using CountedStd =
    std::basic_string<char, std::char_traits<char>, yasl::mem::Allocator<char>>;

template <class Text, class Copy, class Mutate>
void allocations(const char* name, size_t length, int mutation, Copy copy,
                 Mutate mutate) {
  Counter counter;
  const std::string input(length, 'a');
  {
    Text source(input.data(), input.size(), &counter);
    // Keep the copies alive to model a retained set of values; source included
    // in requested bytes and peak, but vector storage excluded for both types.
    std::vector<Text> copies;
    copies.reserve(1000);
    for (size_t i = 0; i < 1000; ++i) {
      copies.push_back(copy(source));
      if (mutation && i % 100 < size_t(mutation)) mutate(copies.back());
    }
    std::printf("%s,%zu,%d,1000,%zu,%zu,%zu,%zu\n", name, length, mutation,
                sizeof(Text), counter.allocations, counter.total, counter.peak);
  }
  if (counter.live) std::abort();
}

void memory() {
  std::puts(
      "type,length,mutation_percent,copies,object_bytes,allocations,requested_"
      "bytes,peak_bytes");
  for (size_t n : {0, 7, 15, 16, 22, 23, 24, 64, 200, 4096}) {
    for (int percent : {0, 10, 100}) {
      if (!n && percent) continue;
      allocations<Mutable::Text>(Mutable::name, n, percent, Mutable::copy,
                                 Mutable::mutate);
      allocations<String>(Cow::name, n, percent, Cow::copy, Cow::mutate);
      allocations<String>(Deep::name, n, percent, Deep::copy, Deep::mutate);
      allocations<CountedStd>(
          "std-resource", n, percent,
          [](const auto& s) { return CountedStd(s); },
          [](auto& s) { s[0] = 'z'; });
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  std::fprintf(stderr,
               "sizeof: yasl=%zu std=%zu counted-std=%zu mutable=%zu; SSO: "
               "yasl=%zu std=%zu\n",
               sizeof(String), sizeof(std::string), sizeof(CountedStd),
               sizeof(Mutable::Text), String::inline_capacity,
               std::string().capacity());
  if (argc > 1 && std::string_view(argv[1]) == "--memory") {
    memory();
    return 0;
  }
  const bool only_parallel =
      argc > 1 && std::string_view(argv[1]) == "--parallel";
  std::puts(
      "operation,type,length,mutation_percent,threads,source,median_ns_per_op,"
      "min_ns_per_op,max_ns_per_op");
  if (!only_parallel) {
    for (size_t n : {0, 7, 15, 16, 22, 23, 24, 64, 200, 4096}) {
      serial<Mutable>(n);
      serial<Cow>(n);
      serial<Deep>(n);
      serial<Std>(n);
    }
  }
  if (argc > 1 && std::string_view(argv[1]) == "--serial") return 0;
  for (size_t n : {15, 16, 200, 4096}) {
    for (int threads : {1, 2, 4, 8}) {
      for (bool shared : {false, true}) {
        for (int mutation : {0, 100}) {
          parallel<Mutable>(n, threads, shared, mutation);
          parallel<Cow>(n, threads, shared, mutation);
          parallel<Deep>(n, threads, shared, mutation);
          parallel<Std>(n, threads, shared, mutation);
        }
      }
    }
  }
}
