#pragma once

#include <yasl/cont/seq/deque.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/ptr/shared.h>
#include <yasl/vocab/function.h>
#include <yasl/vocab/optional.h>

#include "condvar.h"
#include "guarded.h"
#include "thread.h"

namespace yasl {
// Waits for a group of tasks (Go sync.WaitGroup).
//
//     WaitGroup wg;
//     wg.add(3);
//     // each worker: wg.done();
//     wg.wait();
class WaitGroup {
 public:
  void add(size_t n = 1) { *count_.lock() += n; }

  void done() {
    auto g = count_.lock();
    YASL_ASSERT(*g > 0);
    if (--*g == 0) {
      cv_.notify_all();
    }
  }

  void wait() {
    auto g = count_.lock();
    cv_.wait(g, [&] { return *g == 0; });
  }

 private:
  Guarded<size_t> count_;
  CondVar cv_;
};

// One-shot countdown (Java CountDownLatch): wait() returns once count_down()
// was called n times.
class Latch {
 public:
  explicit Latch(size_t n) : count_(n) {}

  void count_down(size_t n = 1) {
    auto g = count_.lock();
    *g = n >= *g ? 0 : *g - n;
    if (*g == 0) {
      cv_.notify_all();
    }
  }

  bool try_wait() { return *count_.lock() == 0; }

  void wait() {
    auto g = count_.lock();
    cv_.wait(g, [&] { return *g == 0; });
  }

 private:
  Guarded<size_t> count_;
  CondVar cv_;
};

namespace detail {
struct Unit {};

template <class T>
struct FutureState {
  using Stored = Conditional<SameAs<T, void>, Unit, T>;

  Mutex m;
  CondVar cv;
  Optional<Stored> value;
  bool taken = false;
};
}  // namespace detail

// The result of a task that may still be running (spawn() returns one).
// If the task throws, the program terminates: tasks must not throw.
template <class T>
class Future {
  using State = detail::FutureState<T>;

 public:
  Future() noexcept = default;

  bool valid() const noexcept { return static_cast<bool>(s_); }

  bool is_ready() const {
    LockGuard<Mutex> g(s_->m);
    return s_->value.has_value() || s_->taken;
  }

  void wait() const {
    LockGuard<Mutex> g(s_->m);
    while (!s_->value.has_value() && !s_->taken) {
      s_->cv.wait(s_->m);
    }
  }

  // False if the result is still not there after `timeout`.
  bool wait_for(Duration timeout) const {
    LockGuard<Mutex> g(s_->m);
    Instant deadline = Instant::now() + timeout;
    while (!s_->value.has_value() && !s_->taken) {
      Duration left = deadline - Instant::now();
      if (left <= Duration() || !s_->cv.wait_for(s_->m, left)) {
        return s_->value.has_value();
      }
    }
    return true;
  }

  // Blocks for the result and takes it; call once.
  T get() {
    wait();
    LockGuard<Mutex> g(s_->m);
    YASL_ASSERT(s_->value.has_value() && !s_->taken);
    s_->taken = true;
    if constexpr (SameAs<T, void>) {
      s_->value.reset();
    } else {
      T out = yasl::move(*s_->value);
      s_->value.reset();
      return out;
    }
  }

 private:
  friend class ThreadPool;

  explicit Future(Shared<State> s) noexcept : s_(yasl::move(s)) {}

  Shared<State> s_;
};

// A fixed set of worker threads running queued tasks (no work stealing). The
// destructor finishes the queued tasks, then joins the workers. The resource is
// used from several threads at once: pass a thread-safe one
// (mem::Synchronized<...>, the default resource).
//
//     ThreadPool pool(4);
//     Future<int> f = pool.spawn([] { return heavy(); });
//     use(f.get());
class ThreadPool {
 public:
  explicit ThreadPool(size_t workers = Thread::hardware_concurrency(),
                      mem::Resource* res = mem::default_resource())
      : res_(res),
        queue_(Queue{Deque<Function<void()>>(res), false}),
        threads_(res) {
    if (workers == 0) {
      workers = 1;
    }
    for (size_t i = 0; i < workers; ++i) {
      threads_.push_back(Thread([this] { run(); }, res));
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  ~ThreadPool() {
    {
      auto g = queue_.lock();
      g->closing = true;
    }
    cv_.notify_all();
    threads_.clear();  // joins
  }

  size_t workers() const noexcept { return threads_.size(); }

  // Queues f() for a worker.
  template <class F>
  void submit(F&& f) {
    {
      auto g = queue_.lock();
      g->tasks.push_back(Function<void()>(yasl::forward<F>(f), res_));
    }
    cv_.notify_one();
  }

  // Queues f() and returns a Future for its result.
  template <class F>
  auto spawn(F&& f) -> Future<decltype(f())> {
    using R = decltype(f());
    using State = detail::FutureState<R>;
    Shared<State> state = make_shared_in<State>(res_);
    submit([state, fn = RemoveCVRef<F>(yasl::forward<F>(f))]() mutable {
      if constexpr (SameAs<R, void>) {
        fn();
        LockGuard<Mutex> g(state->m);
        state->value.emplace();
      } else {
        R r = fn();
        LockGuard<Mutex> g(state->m);
        state->value.emplace(yasl::move(r));
      }
      state->cv.notify_all();
    });
    return Future<R>(yasl::move(state));
  }

 private:
  struct Queue {
    Deque<Function<void()>> tasks;
    bool closing;
  };

  void run() {
    for (;;) {
      Function<void()> task;
      {
        auto g = queue_.lock();
        cv_.wait(g, [&] { return g->closing || !g->tasks.empty(); });
        if (g->tasks.empty()) {
          return;  // closing and drained
        }
        task = g->tasks.take_front();
      }
      task();
    }
  }

  mem::Resource* res_;
  Guarded<Queue> queue_;
  CondVar cv_;
  Vector<Thread> threads_;
};
}  // namespace yasl
